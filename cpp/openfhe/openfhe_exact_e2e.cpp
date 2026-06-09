#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "openfhe.h"
#include "ciphertext-ser.h"
#include "scheme/bgvrns/bgvrns-ser.h"
#include "scheme/bfvrns/bfvrns-ser.h"

#include "benchmark_args.hpp"
#include "csv_reader.hpp"
#include "exact_compare.hpp"
#include "memory_usage.hpp"
#include "timer.hpp"

namespace
{
    constexpr std::uint64_t kPlainModulus = 786433;
    constexpr std::uint32_t kMultiplicativeDepth = 2;

#ifdef HE_BENCHMARK_OPENFHE_E2E_BGV
    constexpr const char *kSchemeName = "BGV";
    constexpr const char *kBinaryName = "openfhe_bgv_e2e";
#else
    constexpr const char *kSchemeName = "BFV";
    constexpr const char *kBinaryName = "openfhe_bfv_e2e";
#endif

    bool is_power_of_two(std::size_t value)
    {
        return value > 0 && (value & (value - 1)) == 0;
    }

    std::int64_t centered_mod_wide(__int128 value, std::uint64_t plain_modulus)
    {
        const auto modulus = static_cast<__int128>(plain_modulus);
        auto reduced = value % modulus;
        if (reduced < 0)
        {
            reduced += modulus;
        }
        if (reduced > modulus / 2)
        {
            reduced -= modulus;
        }
        return static_cast<std::int64_t>(reduced);
    }

    std::int64_t expected_sum(const std::vector<hebench::ExactRow> &rows, std::uint64_t plain_modulus)
    {
        __int128 sum = 0;
        for (const auto &row : rows)
        {
            sum += hebench::centered_mod(row.a, plain_modulus);
            sum = centered_mod_wide(sum, plain_modulus);
        }
        return centered_mod_wide(sum, plain_modulus);
    }

    std::int64_t expected_dot(const std::vector<hebench::ExactRow> &rows, std::uint64_t plain_modulus)
    {
        __int128 sum = 0;
        for (const auto &row : rows)
        {
            const auto a = hebench::centered_mod(row.a, plain_modulus);
            const auto b = hebench::centered_mod(row.b, plain_modulus);
            sum += static_cast<__int128>(a) * static_cast<__int128>(b);
            sum = centered_mod_wide(sum, plain_modulus);
        }
        return centered_mod_wide(sum, plain_modulus);
    }

    std::vector<int64_t> encode_signed_inputs(
        const std::vector<hebench::ExactRow> &rows,
        bool use_b,
        std::size_t slot_count,
        std::uint64_t plain_modulus)
    {
        std::vector<int64_t> values(slot_count, 0);
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto raw = use_b ? rows[i].b : rows[i].a;
            values[i] = hebench::centered_mod(raw, plain_modulus);
        }
        return values;
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

    std::int64_t decrypt_first_slot(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &private_key,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &ciphertext)
    {
        lbcrypto::Plaintext plaintext;
        crypto_context->Decrypt(private_key, ciphertext, &plaintext);
        const auto &packed = plaintext->GetPackedValue();
        if (packed.empty())
        {
            return 0;
        }
        return hebench::centered_mod(packed[0], kPlainModulus);
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
        std::int64_t expected,
        std::int64_t actual,
        const std::string &error = "")
    {
        const auto requests_per_sec = total_ms > 0.0 ? 1000.0 / total_ms : 0.0;
        const auto values_per_sec = total_ms > 0.0 ? static_cast<double>(size) * 1000.0 / total_ms : 0.0;

        std::cout
            << "library=OpenFHE"
            << ",scheme=" << kSchemeName
            << ",operation=" << operation
            << ",size=" << size
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << total_ms
            << ",server_eval_latency_ms=" << server_eval_ms
            << ",ops_per_sec=" << requests_per_sec
            << ",requests_per_sec=" << requests_per_sec
            << ",values_per_sec=" << values_per_sec
            << ",request_bytes=" << request_bytes
            << ",response_bytes=" << response_bytes
            << ",peak_rss_kb=" << hebench::peak_rss_kb()
            << ",rotations_count=" << rotations_count
            << ",components_after=" << components_after
            << ",expected=" << expected
            << ",actual=" << actual;
        if (!correct)
        {
            std::cout << ",error=\"" << error << "\"";
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

        const auto rows = hebench::read_exact_csv(args.corpus_path);
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
            throw std::runtime_error("corpus row count exceeds conservative OpenFHE exact slot budget");
        }

#ifdef HE_BENCHMARK_OPENFHE_E2E_BGV
        lbcrypto::CCParams<lbcrypto::CryptoContextBGVRNS> parameters;
#else
        lbcrypto::CCParams<lbcrypto::CryptoContextBFVRNS> parameters;
#endif
        parameters.SetPlaintextModulus(kPlainModulus);
        parameters.SetMultiplicativeDepth(kMultiplicativeDepth);
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size));

        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);
        crypto_context->Enable(lbcrypto::ADVANCEDSHE);

        auto keys = crypto_context->KeyGen();
        crypto_context->EvalRotateKeyGen(keys.secretKey, rotation_steps(rows.size()));

        auto plain_a = crypto_context->MakePackedPlaintext(
            encode_signed_inputs(rows, false, args.ring_size, kPlainModulus));
        auto plain_b = crypto_context->MakePackedPlaintext(
            encode_signed_inputs(rows, true, args.ring_size, kPlainModulus));

        bool all_correct = true;
        for (const auto operation : {"end_to_end_sum", "end_to_end_dot_product_pt"})
        {
            const bool is_sum = std::string(operation) == "end_to_end_sum";
            const auto expected = is_sum ? expected_sum(rows, kPlainModulus) : expected_dot(rows, kPlainModulus);

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

            const auto correct = actual == expected;
            all_correct = correct && all_correct;
            const auto error = correct
                ? ""
                : "expected " + std::to_string(expected) + " got " + std::to_string(actual);

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
                expected,
                actual,
                error);
        }

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << kBinaryName << " failed: " << error.what() << '\n';
        return 2;
    }
}
