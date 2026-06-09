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
#include "ckks_compare.hpp"
#include "ckks_config.hpp"
#include "csv_reader.hpp"
#include "throughput.hpp"

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

    std::vector<double> expected_product(const std::vector<hebench::CkksRow> &rows)
    {
        std::vector<double> expected(rows.size(), 0.0);
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            expected[i] = rows[i].a * rows[i].b;
        }
        return expected;
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

    std::vector<double> decrypt_decode(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &private_key,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &ciphertext,
        std::size_t length)
    {
        lbcrypto::Plaintext plaintext;
        crypto_context->Decrypt(private_key, ciphertext, &plaintext);
        plaintext->SetLength(length);
        return plaintext->GetRealPackedValue();
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

    std::string metrics_extra(const hebench::CkksMetrics &metrics)
    {
        std::string extra =
            "pass_rate=" + std::to_string(metrics.pass_rate) +
            ",passed=" + std::to_string(metrics.passed) +
            ",mae=" + std::to_string(metrics.mae) +
            ",rmse=" + std::to_string(metrics.rmse) +
            ",max_abs_error=" + std::to_string(metrics.max_abs_error) +
            ",mean_relative_error=" + std::to_string(metrics.mean_relative_error) +
            ",max_relative_error=" + std::to_string(metrics.max_relative_error) +
            ",precision_bits=" + std::to_string(metrics.precision_bits);
        if (!metrics.correct)
        {
            extra += ",error=\"" + metrics.error + "\"";
        }
        return extra;
    }

    void print_row(
        const std::string &operation,
        const hebench::BenchmarkArgs &args,
        std::size_t active_slots,
        std::size_t slot_capacity,
        bool correct,
        const hebench::ThroughputResult &result,
        const std::string &extra = "")
    {
        hebench::print_throughput_row(
            "OpenFHE",
            "CKKS",
            operation,
            active_slots,
            args.ring_size,
            correct,
            hebench::throughput_base_extra(result, args.duration_ms, active_slots, slot_capacity) +
                (g_ckks_config_extra.empty() ? "" : "," + g_ckks_config_extra) +
                (extra.empty() ? "" : "," + extra));
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
            throw std::runtime_error("throughput dot-product requires a power-of-two row count");
        }
        if (rows.size() > args.ring_size / 2)
        {
            throw std::runtime_error("corpus row count exceeds OpenFHE CKKS batch size");
        }

        const auto slot_capacity = args.ring_size / 2;
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
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(slot_capacity));

        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);
        crypto_context->Enable(lbcrypto::ADVANCEDSHE);

        auto keys = crypto_context->KeyGen();
        crypto_context->EvalRotateKeyGen(keys.secretKey, rotation_steps(rows.size()));

        auto plain_a = crypto_context->MakeCKKSPackedPlaintext(values_for(rows, false, slot_capacity));
        auto plain_b = crypto_context->MakeCKKSPackedPlaintext(values_for(rows, true, slot_capacity));
        const auto encrypted_a = crypto_context->Encrypt(keys.publicKey, plain_a);

        bool all_correct = true;

        {
            lbcrypto::Ciphertext<lbcrypto::DCRTPoly> sample;
            const auto result = hebench::run_for_duration(args.duration_ms, [&]() {
                sample = crypto_context->Encrypt(keys.publicKey, plain_a);
            });

            const auto decoded = decrypt_decode(crypto_context, keys.secretKey, sample, rows.size());
            const auto metrics = hebench::compare_ckks_vectors(decoded, values_for(rows, false, rows.size()));
            all_correct = all_correct && metrics.correct;
            print_row("throughput_encrypt", args, rows.size(), slot_capacity, metrics.correct, result, metrics_extra(metrics));
        }

        {
            lbcrypto::Ciphertext<lbcrypto::DCRTPoly> sample;
            const auto result = hebench::run_for_duration(args.duration_ms, [&]() {
                sample = crypto_context->EvalMult(encrypted_a, plain_b);
            });

            const auto decoded = decrypt_decode(crypto_context, keys.secretKey, sample, rows.size());
            const auto metrics = hebench::compare_ckks_vectors(decoded, expected_product(rows));
            all_correct = all_correct && metrics.correct;
            print_row("throughput_mul_ct_pt", args, rows.size(), slot_capacity, metrics.correct, result, metrics_extra(metrics));
        }

        {
            lbcrypto::Ciphertext<lbcrypto::DCRTPoly> sample;
            const auto expected = expected_dot(rows);
            const auto result = hebench::run_for_duration(args.duration_ms, [&]() {
                sample = rotate_sum(crypto_context, crypto_context->EvalMult(encrypted_a, plain_b), rows.size());
            });

            const auto decoded = decrypt_decode(crypto_context, keys.secretKey, sample, 1);
            const auto actual = decoded.empty() ? 0.0 : decoded[0];
            const auto abs_error = std::abs(actual - expected);
            const auto relative_error = abs_error / std::max(std::abs(expected), 1e-12);
            const auto correct = abs_error <= 1e-3 || relative_error <= 1e-6;
            all_correct = all_correct && correct;
            print_row(
                "throughput_dot_product_pt",
                args,
                rows.size(),
                slot_capacity,
                correct,
                result,
                "requests_per_sec=" + std::to_string(hebench::safe_rate(result.completed, result.elapsed_seconds)) +
                    ",input_values_per_sec=" + std::to_string(
                        hebench::safe_rate(result.completed * static_cast<double>(rows.size()), result.elapsed_seconds)) +
                    ",rotations_count=" + std::to_string(rotation_steps(rows.size()).size()) +
                    ",expected=" + std::to_string(expected) +
                    ",actual=" + std::to_string(actual) +
                    ",mae=" + std::to_string(abs_error) +
                    ",rmse=" + std::to_string(abs_error) +
                    ",max_abs_error=" + std::to_string(abs_error) +
                    ",relative_error=" + std::to_string(relative_error));
        }

        {
            std::string sample_bytes;
            std::size_t total_bytes = 0;
            const auto result = hebench::run_for_duration(args.duration_ms, [&]() {
                sample_bytes = serialize_to_string(encrypted_a);
                total_bytes += sample_bytes.size();
            });

            lbcrypto::Ciphertext<lbcrypto::DCRTPoly> loaded;
            deserialize_from_string(sample_bytes, loaded);
            const auto decoded = decrypt_decode(crypto_context, keys.secretKey, loaded, rows.size());
            const auto metrics = hebench::compare_ckks_vectors(decoded, values_for(rows, false, rows.size()));
            all_correct = all_correct && metrics.correct;
            print_row(
                "throughput_serialize_ciphertext",
                args,
                rows.size(),
                slot_capacity,
                metrics.correct,
                result,
                "total_serialized_bytes=" + std::to_string(total_bytes) +
                    ",objects_per_sec=" + std::to_string(hebench::safe_rate(result.completed, result.elapsed_seconds)) +
                    ",MB_per_sec=" + std::to_string(hebench::safe_rate(total_bytes / 1000000.0, result.elapsed_seconds)) +
                    "," + metrics_extra(metrics));
        }

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
