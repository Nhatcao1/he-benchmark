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
#include "ckks_config.hpp"
#include "ckks_compare.hpp"
#include "csv_reader.hpp"
#include "timer.hpp"

namespace
{
    using hebench::CkksOperation;

    std::string g_ckks_config_extra;

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

    void print_metric_row(
        const std::string &operation,
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        double elapsed_ms,
        double ops_count,
        std::size_t values_count,
        const std::string &extra = "")
    {
        const auto ops_per_sec = elapsed_ms > 0.0 ? ops_count * 1000.0 / elapsed_ms : 0.0;
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

    bool compare_input(
        const std::vector<double> &actual,
        const std::vector<hebench::CkksRow> &rows,
        bool use_b,
        std::string &error)
    {
        if (actual.size() < rows.size())
        {
            error = "decoded slot count is smaller than corpus row count";
            return false;
        }

        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto expected = use_b ? rows[i].b : rows[i].a;
            if (std::abs(actual[i] - expected) > 1e-3)
            {
                error = "slot " + std::to_string(i) + " expected " + std::to_string(expected) +
                    " got " + std::to_string(actual[i]);
                return false;
            }
        }

        return true;
    }

    bool run_operation(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &private_key,
        const std::vector<hebench::CkksRow> &rows,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &encrypted_a,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &encrypted_b,
        lbcrypto::Plaintext plain_b,
        CkksOperation operation,
        std::size_t ring_size)
    {
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> result;
        const auto scale_before = encrypted_a->GetScalingFactor();
        const auto level_before = encrypted_a->GetLevel();
        const hebench::Timer timer;

        switch (operation)
        {
        case CkksOperation::add:
            result = crypto_context->EvalAdd(encrypted_a, encrypted_b);
            break;
        case CkksOperation::sub:
            result = crypto_context->EvalSub(encrypted_a, encrypted_b);
            break;
        case CkksOperation::mul:
        case CkksOperation::rescale:
            result = crypto_context->EvalMult(encrypted_a, encrypted_b);
            break;
        case CkksOperation::add_plain:
            result = crypto_context->EvalAdd(encrypted_a, plain_b);
            break;
        case CkksOperation::sub_plain:
            result = crypto_context->EvalSub(encrypted_a, plain_b);
            break;
        case CkksOperation::mul_plain:
            result = crypto_context->EvalMult(encrypted_a, plain_b);
            break;
        case CkksOperation::square_a:
            result = crypto_context->EvalMult(encrypted_a, encrypted_a);
            break;
        case CkksOperation::negate_a:
            result = crypto_context->EvalNegate(encrypted_a);
            break;
        }

        const auto elapsed_ms = timer.elapsed_ms();
        const auto decoded = decrypt_decode(crypto_context, private_key, result, rows.size());
        const auto metrics = hebench::compare_ckks_slots(decoded, rows, operation);

        print_metric_row(
            hebench::operation_name(operation),
            rows.size(),
            ring_size,
            metrics.correct,
            elapsed_ms,
            1.0,
            rows.size(),
            "scale_before=" + std::to_string(scale_before) +
                ",scale_after=" + std::to_string(result->GetScalingFactor()) +
                ",level_before=" + std::to_string(level_before) +
                ",level_after=" + std::to_string(result->GetLevel()) +
                "," + metrics_extra(metrics));
        return metrics.correct;
    }

    bool run_relinearization(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &private_key,
        const std::vector<hebench::CkksRow> &rows,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &encrypted_a,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &encrypted_b,
        std::size_t ring_size)
    {
        auto product = crypto_context->EvalMultNoRelin(encrypted_a, encrypted_b);
        const auto components_before = product->NumberCiphertextElements();

        const hebench::Timer timer;
        auto result = crypto_context->Relinearize(product);
        const auto elapsed_ms = timer.elapsed_ms();

        const auto decoded = decrypt_decode(crypto_context, private_key, result, rows.size());
        const auto metrics = hebench::compare_ckks_slots(decoded, rows, CkksOperation::mul);
        print_metric_row(
            "relin",
            rows.size(),
            ring_size,
            metrics.correct,
            elapsed_ms,
            1.0,
            rows.size(),
            "components_before=" + std::to_string(components_before) +
                ",components_after=" + std::to_string(result->NumberCiphertextElements()) +
                ",reduction_ratio=" + std::to_string(
                    components_before > 0
                        ? static_cast<double>(result->NumberCiphertextElements()) / static_cast<double>(components_before)
                        : 0.0) +
                "," + metrics_extra(metrics));
        return metrics.correct;
    }

    std::vector<double> expected_rotation(
        const std::vector<hebench::CkksRow> &rows,
        std::size_t slot_count,
        int step,
        bool left)
    {
        const auto base = values_for(rows, false, slot_count);
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

        print_metric_row(
            "rotate_" + std::to_string(step),
            rows.size(),
            ring_size,
            metrics.correct,
            elapsed_ms,
            1.0,
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

        const auto ckks_config = hebench::ckks_config_for(args, 3, 40);
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

        const hebench::Timer keygen_timer;
        auto keys = crypto_context->KeyGen();
        const auto keygen_ms = keygen_timer.elapsed_ms();

        const hebench::Timer relin_keygen_timer;
        crypto_context->EvalMultKeyGen(keys.secretKey);
        const auto relin_keygen_ms = relin_keygen_timer.elapsed_ms();

        const hebench::Timer rotation_keygen_timer;
        crypto_context->EvalRotateKeyGen(keys.secretKey, {1, -1, 8});
        const auto rotation_keygen_ms = rotation_keygen_timer.elapsed_ms();

        print_metric_row("keygen", rows.size(), args.ring_size, true, keygen_ms, 1.0, 0);
        print_metric_row("relin_keygen", rows.size(), args.ring_size, true, relin_keygen_ms, 1.0, 0);
        print_metric_row("rotation_keygen", rows.size(), args.ring_size, true, rotation_keygen_ms, 1.0, 0);

        const hebench::Timer encode_timer;
        auto plain_a = crypto_context->MakeCKKSPackedPlaintext(values_for(rows, false, args.ring_size / 2));
        auto plain_b = crypto_context->MakeCKKSPackedPlaintext(values_for(rows, true, args.ring_size / 2));
        const auto encode_ms = encode_timer.elapsed_ms();
        print_metric_row("encode", rows.size(), args.ring_size, true, encode_ms, 2.0, rows.size() * 2);

        const hebench::Timer decode_timer;
        const auto decoded_a = plain_a->GetRealPackedValue();
        const auto decoded_b = plain_b->GetRealPackedValue();
        const auto decode_ms = decode_timer.elapsed_ms();
        std::string input_error;
        const auto decode_correct =
            compare_input(decoded_a, rows, false, input_error) &&
            compare_input(decoded_b, rows, true, input_error);
        print_metric_row("decode", rows.size(), args.ring_size, decode_correct, decode_ms, 2.0, rows.size() * 2,
            decode_correct ? "" : "error=\"" + input_error + "\"");

        const hebench::Timer encrypt_timer;
        const auto encrypted_a = crypto_context->Encrypt(keys.publicKey, plain_a);
        const auto encrypted_b = crypto_context->Encrypt(keys.publicKey, plain_b);
        const auto encrypt_ms = encrypt_timer.elapsed_ms();
        print_metric_row("encrypt", rows.size(), args.ring_size, true, encrypt_ms, 2.0, rows.size() * 2);

        const hebench::Timer decrypt_timer;
        const auto decrypted_a = decrypt_decode(crypto_context, keys.secretKey, encrypted_a, rows.size());
        const auto decrypted_b = decrypt_decode(crypto_context, keys.secretKey, encrypted_b, rows.size());
        const auto decrypt_ms = decrypt_timer.elapsed_ms();
        const auto decrypt_correct =
            compare_input(decrypted_a, rows, false, input_error) &&
            compare_input(decrypted_b, rows, true, input_error);
        print_metric_row("decrypt", rows.size(), args.ring_size, decrypt_correct, decrypt_ms, 2.0, rows.size() * 2,
            decrypt_correct ? "" : "error=\"" + input_error + "\"");

        bool all_correct = decode_correct && decrypt_correct;
        all_correct = run_relinearization(
            crypto_context, keys.secretKey, rows, encrypted_a, encrypted_b, args.ring_size) && all_correct;

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

        for (const auto operation : {
                 CkksOperation::add,
                 CkksOperation::sub,
                 CkksOperation::mul,
                 CkksOperation::add_plain,
                 CkksOperation::sub_plain,
                 CkksOperation::mul_plain,
                 CkksOperation::square_a,
                 CkksOperation::negate_a,
                 CkksOperation::rescale,
             })
        {
            all_correct = run_operation(
                crypto_context,
                keys.secretKey,
                rows,
                encrypted_a,
                encrypted_b,
                plain_b,
                operation,
                args.ring_size) && all_correct;
        }

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "openfhe_ckks failed: " << error.what() << '\n';
        return 2;
    }
}
