#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "openfhe.h"
#include "ciphertext-ser.h"
#include "scheme/ckksrns/ckksrns-ser.h"

#include "benchmark_args.hpp"
#include "ckks_config.hpp"
#include "csv_reader.hpp"
#include "memory_usage.hpp"
#include "timer.hpp"

namespace
{
    std::string g_ckks_config_extra;

    bool is_power_of_two(std::size_t value)
    {
        return value > 0 && (value & (value - 1)) == 0;
    }

    std::vector<double> values_for(
        const std::vector<hebench::CkksRow> &rows,
        bool use_b,
        std::size_t slot_count)
    {
        std::vector<double> values(slot_count, 0.0);
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            values[i] = use_b ? rows[i].b : rows[i].a;
        }
        return values;
    }

    double expected_sum(const std::vector<hebench::CkksRow> &rows)
    {
        double sum = 0.0;
        for (const auto &row : rows)
        {
            sum += row.a;
        }
        return sum;
    }

    double expected_dot(const std::vector<hebench::CkksRow> &rows)
    {
        double sum = 0.0;
        for (const auto &row : rows)
        {
            sum += row.a * row.b;
        }
        return sum;
    }

    std::vector<int32_t> rotation_steps(std::size_t size)
    {
        std::vector<int32_t> steps;
        for (std::size_t step = 1; step < size; step *= 2)
        {
            steps.push_back(static_cast<int32_t>(step));
        }
        return steps;
    }

    template <typename T>
    std::string serialize_to_string(const T &value)
    {
        std::stringstream stream;
        lbcrypto::Serial::Serialize(value, stream, lbcrypto::SerType::BINARY);
        return stream.str();
    }

    template <typename T>
    void deserialize_from_string(const std::string &bytes, T &value)
    {
        std::stringstream stream(bytes);
        lbcrypto::Serial::Deserialize(value, stream, lbcrypto::SerType::BINARY);
    }

    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> rotate_sum(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &input,
        std::size_t size)
    {
        auto result = input;
        for (const auto step : rotation_steps(size))
        {
            const auto rotated = crypto_context->EvalRotate(result, step);
            result = crypto_context->EvalAdd(result, rotated);
        }
        return result;
    }

    double decrypt_first_slot(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &private_key,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &ciphertext)
    {
        lbcrypto::Plaintext plaintext;
        crypto_context->Decrypt(private_key, ciphertext, &plaintext);
        plaintext->SetLength(1);
        const auto packed = plaintext->GetRealPackedValue();
        return packed.empty() ? 0.0 : packed[0];
    }

    void print_row(
        const std::string &operation,
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        double total_ms,
        double server_eval_ms,
        std::size_t request_bytes,
        std::size_t response_bytes,
        std::size_t rotations_count,
        std::size_t components_after,
        double scale_after,
        std::size_t level_after,
        double expected,
        double actual)
    {
        const auto requests_per_sec = total_ms > 0.0 ? 1000.0 / total_ms : 0.0;
        const auto values_per_sec = total_ms > 0.0 ? static_cast<double>(size) * 1000.0 / total_ms : 0.0;
        const auto abs_error = std::abs(actual - expected);
        const auto relative_error = abs_error / std::max(std::abs(expected), 1e-12);
        const auto precision_bits = relative_error > 0.0 ? -std::log2(relative_error) : INFINITY;

        std::cout
            << "library=OpenFHE"
            << ",scheme=CKKS"
            << ",operation=" << operation
            << ",size=" << size
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << total_ms
            << ",server_eval_latency_ms=" << server_eval_ms
            << ",ops_per_sec=" << requests_per_sec
            << ",requests_per_sec=" << requests_per_sec
            << ",values_per_sec=" << values_per_sec;
        if (!g_ckks_config_extra.empty())
        {
            std::cout << ',' << g_ckks_config_extra;
        }
        std::cout
            << ",request_bytes=" << request_bytes
            << ",response_bytes=" << response_bytes
            << ",peak_rss_kb=" << hebench::peak_rss_kb()
            << ",rotations_count=" << rotations_count
            << ",components_after=" << components_after
            << ",scale_after=" << scale_after
            << ",level_after=" << level_after
            << ",expected=" << expected
            << ",actual=" << actual
            << ",abs_error=" << abs_error
            << ",relative_error=" << relative_error
            << ",precision_bits=" << precision_bits;
        if (!correct)
        {
            std::cout << ",error=\"expected " << expected << " got " << actual << "\"";
        }
        std::cout << '\n';
    }
}

int main(int argc, char **argv)
{
    try
    {
        const auto args = hebench::parse_benchmark_args(argc, argv);
        if (args.show_help)
        {
            std::cout << hebench::benchmark_usage(argv[0]);
            return 0;
        }

        const auto rows = hebench::read_ckks_csv(args.corpus_path);
        if (rows.empty())
        {
            throw std::runtime_error("corpus has no rows: " + args.corpus_path);
        }
        if (!is_power_of_two(rows.size()))
        {
            throw std::runtime_error("end-to-end workloads require a power-of-two row count");
        }
        if (rows.size() > args.ring_size / 2)
        {
            throw std::runtime_error("corpus row count exceeds OpenFHE CKKS batch size");
        }

        const auto ckks_config = hebench::ckks_config_for(args, 2, 40);
        g_ckks_config_extra = hebench::ckks_config_extra(ckks_config);

        lbcrypto::CCParams<lbcrypto::CryptoContextCKKSRNS> parameters;
        parameters.SetMultiplicativeDepth(static_cast<std::uint32_t>(ckks_config.multiplicative_depth));
        parameters.SetScalingModSize(static_cast<std::uint32_t>(ckks_config.scale_bits));
        if (ckks_config.explicit_first_mod)
        {
            parameters.SetFirstModSize(static_cast<std::uint32_t>(ckks_config.first_mod_bits));
        }
        if (ckks_config.relaxed_security)
        {
            parameters.SetSecurityLevel(lbcrypto::HEStd_NotSet);
        }
        else
        {
            parameters.SetSecurityLevel(lbcrypto::HEStd_128_classic);
        }
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size / 2));

        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);
        crypto_context->Enable(lbcrypto::ADVANCEDSHE);

        auto keys = crypto_context->KeyGen();
        crypto_context->EvalRotateKeyGen(keys.secretKey, rotation_steps(rows.size()));

        auto plain_a = crypto_context->MakeCKKSPackedPlaintext(values_for(rows, false, args.ring_size / 2));
        auto plain_b = crypto_context->MakeCKKSPackedPlaintext(values_for(rows, true, args.ring_size / 2));

        bool all_correct = true;
        for (const auto operation : {"end_to_end_sum", "end_to_end_dot_product_pt"})
        {
            const bool is_sum = std::string(operation) == "end_to_end_sum";
            const auto expected = is_sum ? expected_sum(rows) : expected_dot(rows);

            const hebench::Timer total_timer;
            const auto encrypted_request = crypto_context->Encrypt(keys.publicKey, plain_a);
            const auto request_bytes = serialize_to_string(encrypted_request);

            lbcrypto::Ciphertext<lbcrypto::DCRTPoly> server_request;
            deserialize_from_string(request_bytes, server_request);

            const hebench::Timer server_timer;
            lbcrypto::Ciphertext<lbcrypto::DCRTPoly> server_result;
            if (is_sum)
            {
                server_result = rotate_sum(crypto_context, server_request, rows.size());
            }
            else
            {
                server_result = crypto_context->EvalMult(server_request, plain_b);
                server_result = rotate_sum(crypto_context, server_result, rows.size());
            }
            const auto server_eval_ms = server_timer.elapsed_ms();
            const auto response_bytes = serialize_to_string(server_result);

            lbcrypto::Ciphertext<lbcrypto::DCRTPoly> client_response;
            deserialize_from_string(response_bytes, client_response);
            const auto actual = decrypt_first_slot(crypto_context, keys.secretKey, client_response);
            const auto total_ms = total_timer.elapsed_ms();

            const auto abs_error = std::abs(actual - expected);
            const auto relative_error = abs_error / std::max(std::abs(expected), 1e-12);
            const bool correct = abs_error <= 1e-3 || relative_error <= 1e-3;
            all_correct = correct && all_correct;

            print_row(
                operation,
                rows.size(),
                args.ring_size,
                correct,
                total_ms,
                server_eval_ms,
                request_bytes.size(),
                response_bytes.size(),
                rotation_steps(rows.size()).size(),
                client_response->NumberCiphertextElements(),
                client_response->GetScalingFactor(),
                client_response->GetLevel(),
                expected,
                actual);
        }

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "openfhe_ckks_e2e failed: " << error.what() << '\n';
        return 2;
    }
}
