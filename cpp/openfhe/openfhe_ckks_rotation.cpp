#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "openfhe.h"

#include "benchmark_args.hpp"
#include "ckks_compare.hpp"
#include "ckks_config.hpp"
#include "csv_reader.hpp"
#include "timer.hpp"

namespace
{
    std::string g_ckks_config_extra;

    std::vector<double> values_for(
        const std::vector<hebench::CkksRow> &rows,
        std::size_t slot_count)
    {
        std::vector<double> values(slot_count, 0.0);
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            values[i] = rows[i].a;
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
        if (!g_ckks_config_extra.empty())
        {
            std::cout << ',' << g_ckks_config_extra;
        }
        if (!extra.empty())
        {
            std::cout << ',' << extra;
        }
        std::cout << '\n';
    }

    std::vector<double> expected_rotation(
        const std::vector<hebench::CkksRow> &rows,
        std::size_t slot_count,
        int step,
        bool left)
    {
        const auto base = values_for(rows, slot_count);
        std::vector<double> expected(rows.size(), 0.0);
        const auto normalized_step = static_cast<std::size_t>(
            ((step % static_cast<int>(slot_count)) + static_cast<int>(slot_count)) %
            static_cast<int>(slot_count));

        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto source = left
                ? (i + normalized_step) % slot_count
                : (i + slot_count - normalized_step) % slot_count;
            expected[i] = base[source];
        }
        return expected;
    }

    bool run_rotation(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &private_key,
        const std::vector<hebench::CkksRow> &rows,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &encrypted_a,
        int step,
        std::size_t ring_size,
        std::size_t slot_count)
    {
        const hebench::Timer timer;
        const auto result = crypto_context->EvalRotate(encrypted_a, step);
        const auto elapsed_ms = timer.elapsed_ms();

        const auto decoded = decrypt_decode(crypto_context, private_key, result, rows.size());
        auto metrics = hebench::compare_ckks_vectors(decoded, expected_rotation(rows, slot_count, step, true));
        std::string direction = "left";
        if (!metrics.correct)
        {
            const auto right_metrics = hebench::compare_ckks_vectors(decoded, expected_rotation(rows, slot_count, step, false));
            if (right_metrics.correct || right_metrics.pass_rate > metrics.pass_rate)
            {
                metrics = right_metrics;
                direction = "right";
            }
        }

        print_row(
            "rotate_" + std::to_string(step),
            rows.size(),
            ring_size,
            metrics.correct,
            elapsed_ms,
            rows.size(),
            "rotation_step=" + std::to_string(step) +
                ",matched_direction=" + direction +
                "," + metrics_extra(metrics));
        return metrics.correct;
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
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size / 2));

        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);
        crypto_context->Enable(lbcrypto::ADVANCEDSHE);

        auto keys = crypto_context->KeyGen();

        const hebench::Timer rotation_keygen_timer;
        crypto_context->EvalRotateKeyGen(keys.secretKey, {1, -1, 8});
        const auto rotation_keygen_ms = rotation_keygen_timer.elapsed_ms();
        print_row("rotation_keygen", rows.size(), args.ring_size, true, rotation_keygen_ms, 0);

        const auto plain_a = crypto_context->MakeCKKSPackedPlaintext(values_for(rows, args.ring_size / 2));
        const auto encrypted_a = crypto_context->Encrypt(keys.publicKey, plain_a);

        bool all_correct = true;
        for (const auto step : {1, -1, 8})
        {
            all_correct = run_rotation(
                crypto_context,
                keys.secretKey,
                rows,
                encrypted_a,
                step,
                args.ring_size,
                args.ring_size / 2) && all_correct;
        }

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "openfhe_ckks_rotation failed: " << error.what() << '\n';
        return 2;
    }
}
