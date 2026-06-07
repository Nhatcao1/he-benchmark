#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "openfhe.h"

#include "benchmark_args.hpp"
#include "ckks_compare.hpp"
#include "csv_reader.hpp"
#include "depth_compare.hpp"
#include "timer.hpp"

namespace
{
    constexpr std::uint32_t kScaleModSize = 30;
    constexpr std::uint32_t kFirstModSize = 49;

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

    void print_metric_row(
        const std::string &operation,
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        double elapsed_ms,
        std::size_t values_count,
        const std::string &extra = "")
    {
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto values_per_sec = elapsed_ms > 0.0 ? static_cast<double>(values_count) * 1000.0 / elapsed_ms : 0.0;

        std::cout
            << "library=OpenFHE"
            << ",scheme=CKKS"
            << ",operation=" << operation
            << ",size=" << size
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << elapsed_ms
            << ",ops_per_sec=" << ops_per_sec
            << ",values_per_sec=" << values_per_sec;
        if (!extra.empty())
        {
            std::cout << ',' << extra;
        }
        std::cout << '\n';
    }

    std::vector<double> values_for(
        const std::vector<hebench::CkksDepthRow> &rows,
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

    std::size_t corpus_depth_count(const std::vector<hebench::CkksDepthRow> &rows)
    {
        const auto count = rows.front().expected_depth.size();
        for (const auto &row : rows)
        {
            if (row.expected_depth.size() != count)
            {
                throw std::runtime_error("inconsistent CKKS depth column count in corpus");
            }
        }
        return count;
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

        const auto rows = hebench::read_ckks_depth_csv(args.corpus_path);
        if (rows.empty())
        {
            throw std::runtime_error("corpus has no rows: " + args.corpus_path);
        }
        if (rows.size() > args.ring_size / 2)
        {
            throw std::runtime_error("corpus row count exceeds OpenFHE CKKS batch size");
        }
        const auto max_depth = std::min(args.max_depth, corpus_depth_count(rows));

        lbcrypto::CCParams<lbcrypto::CryptoContextCKKSRNS> parameters;
        parameters.SetMultiplicativeDepth(static_cast<std::uint32_t>(max_depth));
        parameters.SetScalingModSize(kScaleModSize);
        parameters.SetFirstModSize(kFirstModSize);
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size / 2));

        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);
        crypto_context->Enable(lbcrypto::ADVANCEDSHE);

        auto keys = crypto_context->KeyGen();
        crypto_context->EvalMultKeyGen(keys.secretKey);

        auto plain_a = crypto_context->MakeCKKSPackedPlaintext(values_for(rows, false, args.ring_size / 2));
        auto plain_b = crypto_context->MakeCKKSPackedPlaintext(values_for(rows, true, args.ring_size / 2));
        const auto encrypted_a = crypto_context->Encrypt(keys.publicKey, plain_a);
        const auto encrypted_b = crypto_context->Encrypt(keys.publicKey, plain_b);

        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> current;
        bool keep_running = true;
        for (std::size_t depth = 1; depth <= max_depth && keep_running; ++depth)
        {
            const auto left = depth == 1 ? encrypted_a : current;
            const auto right = depth == 1 ? encrypted_b : current;
            const auto scale_before = left->GetScalingFactor();
            const auto level_before = left->GetLevel();

            const hebench::Timer timer;
            const auto next = crypto_context->EvalMult(left, right);
            const auto elapsed_ms = timer.elapsed_ms();

            const auto decoded = decrypt_decode(crypto_context, keys.secretKey, next, rows.size());
            const auto expected = hebench::ckks_depth_expected(rows, depth);
            const auto metrics = hebench::compare_ckks_vectors(decoded, expected);

            print_metric_row(
                "depth_mul",
                rows.size(),
                args.ring_size,
                metrics.correct,
                elapsed_ms,
                rows.size(),
                "depth=" + std::to_string(depth) +
                    ",max_depth=" + std::to_string(max_depth) +
                    ",scale_bits=" + std::to_string(kScaleModSize) +
                    ",scale_before=" + std::to_string(scale_before) +
                    ",scale_after=" + std::to_string(next->GetScalingFactor()) +
                    ",level_before=" + std::to_string(level_before) +
                    ",level_after=" + std::to_string(next->GetLevel()) +
                    ",components_after=" + std::to_string(next->NumberCiphertextElements()) +
                    "," + metrics_extra(metrics));

            current = next;
            keep_running = metrics.correct;
        }

        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "openfhe_ckks_depth failed: " << error.what() << '\n';
        return 2;
    }
}
