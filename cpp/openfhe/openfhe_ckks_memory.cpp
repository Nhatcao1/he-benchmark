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
#include "memory_usage.hpp"
#include "timer.hpp"

namespace
{
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

    void print_row(
        const std::string &operation,
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        double elapsed_ms,
        std::size_t baseline_kb,
        const std::string &extra = "")
    {
        const auto peak_kb = hebench::peak_rss_kb();
        const auto delta_kb = peak_kb >= baseline_kb ? peak_kb - baseline_kb : 0;
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;

        std::cout
            << "library=OpenFHE"
            << ",scheme=CKKS"
            << ",operation=" << operation
            << ",size=" << size
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << elapsed_ms
            << ",ops_per_sec=" << ops_per_sec
            << ",values_per_sec=0";
        if (!g_ckks_config_extra.empty())
        {
            std::cout << ',' << g_ckks_config_extra;
        }
        std::cout
            << ",peak_rss_kb=" << peak_kb
            << ",delta_peak_rss_kb=" << delta_kb;
        if (!extra.empty())
        {
            std::cout << ',' << extra;
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
        if (rows.size() > args.ring_size / 2)
        {
            throw std::runtime_error("corpus row count exceeds OpenFHE CKKS batch size");
        }
        const auto baseline_kb = hebench::peak_rss_kb();

        const hebench::Timer context_timer;
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
        const auto context_ms = context_timer.elapsed_ms();
        print_row("memory_context", rows.size(), args.ring_size, true, context_ms, baseline_kb);

        const hebench::Timer keygen_timer;
        auto keys = crypto_context->KeyGen();
        const auto keygen_ms = keygen_timer.elapsed_ms();
        print_row("memory_keygen", rows.size(), args.ring_size, true, keygen_ms, baseline_kb);

        const hebench::Timer relin_timer;
        crypto_context->EvalMultKeyGen(keys.secretKey);
        const auto relin_ms = relin_timer.elapsed_ms();
        print_row("memory_relin_keygen", rows.size(), args.ring_size, true, relin_ms, baseline_kb);

        const hebench::Timer rotation_timer;
        crypto_context->EvalRotateKeyGen(keys.secretKey, {1});
        const auto rotation_ms = rotation_timer.elapsed_ms();
        print_row("memory_rotation_keygen", rows.size(), args.ring_size, true, rotation_ms, baseline_kb);

        lbcrypto::Plaintext plain_a;
        lbcrypto::Plaintext plain_b;
        const hebench::Timer encode_timer;
        plain_a = crypto_context->MakeCKKSPackedPlaintext(values_for(rows, false, args.ring_size / 2));
        plain_b = crypto_context->MakeCKKSPackedPlaintext(values_for(rows, true, args.ring_size / 2));
        const auto encode_ms = encode_timer.elapsed_ms();
        print_row("memory_encode", rows.size(), args.ring_size, true, encode_ms, baseline_kb);

        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> encrypted_a;
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> encrypted_b;
        const hebench::Timer encrypt_timer;
        encrypted_a = crypto_context->Encrypt(keys.publicKey, plain_a);
        encrypted_b = crypto_context->Encrypt(keys.publicKey, plain_b);
        const auto encrypt_ms = encrypt_timer.elapsed_ms();
        print_row("memory_encrypt", rows.size(), args.ring_size, true, encrypt_ms, baseline_kb);

        const hebench::Timer multiply_timer;
        const auto product = crypto_context->EvalMult(encrypted_a, encrypted_b);
        const auto multiply_ms = multiply_timer.elapsed_ms();
        const auto decoded = decrypt_decode(crypto_context, keys.secretKey, product, rows.size());
        const auto metrics = hebench::compare_ckks_slots(decoded, rows, hebench::CkksOperation::mul);
        print_row(
            "memory_multiply",
            rows.size(),
            args.ring_size,
            metrics.correct,
            multiply_ms,
            baseline_kb,
            "components_after=" + std::to_string(product->NumberCiphertextElements()) +
                ",scale_after=" + std::to_string(product->GetScalingFactor()) +
                ",level_after=" + std::to_string(product->GetLevel()) +
                ",mae=" + std::to_string(metrics.mae) +
                ",precision_bits=" + std::to_string(metrics.precision_bits) +
                (metrics.correct ? "" : ",error=\"" + metrics.error + "\""));

        const hebench::Timer rotate_timer;
        const auto rotated = crypto_context->EvalRotate(encrypted_a, 1);
        const auto rotate_ms = rotate_timer.elapsed_ms();
        print_row("memory_rotate", rows.size(), args.ring_size, rotated != nullptr, rotate_ms, baseline_kb, "rotation_step=1");

        const hebench::Timer decrypt_timer;
        const auto decrypted = decrypt_decode(crypto_context, keys.secretKey, encrypted_a, rows.size());
        const auto decrypt_ms = decrypt_timer.elapsed_ms();
        print_row("memory_decrypt", rows.size(), args.ring_size, decrypted.size() >= rows.size(), decrypt_ms, baseline_kb);

        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "openfhe_ckks_memory failed: " << error.what() << '\n';
        return 2;
    }
}
