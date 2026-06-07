#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "openfhe.h"

#include "benchmark_args.hpp"
#include "csv_reader.hpp"
#include "exact_compare.hpp"
#include "memory_usage.hpp"
#include "timer.hpp"

namespace
{
    using hebench::ExactOperation;

    constexpr std::uint64_t kPlainModulus = 786433;
    constexpr std::uint32_t kMultiplicativeDepth = 2;

#ifdef HE_BENCHMARK_OPENFHE_MEMORY_BGV
    constexpr const char *kSchemeName = "BGV";
    constexpr const char *kBinaryName = "openfhe_bgv_memory";
#else
    constexpr const char *kSchemeName = "BFV";
    constexpr const char *kBinaryName = "openfhe_bfv_memory";
#endif

    std::vector<int64_t> encode_signed_inputs(
        const std::vector<hebench::ExactRow> &rows,
        bool use_b,
        std::size_t slot_count,
        std::uint64_t plain_modulus)
    {
        std::vector<int64_t> values(slot_count, 0);
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto value = use_b ? rows[i].b : rows[i].a;
            values[i] = hebench::centered_mod(value, plain_modulus);
        }
        return values;
    }

    std::vector<std::uint64_t> decrypt_decode(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &private_key,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &ciphertext,
        std::uint64_t plain_modulus)
    {
        lbcrypto::Plaintext plaintext;
        crypto_context->Decrypt(private_key, ciphertext, &plaintext);

        const auto &packed = plaintext->GetPackedValue();
        std::vector<std::uint64_t> decoded;
        decoded.reserve(packed.size());
        for (const auto value : packed)
        {
            const auto centered = hebench::centered_mod(value, plain_modulus);
            decoded.push_back(centered < 0
                ? static_cast<std::uint64_t>(centered + static_cast<std::int64_t>(plain_modulus))
                : static_cast<std::uint64_t>(centered));
        }
        return decoded;
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
            << ",scheme=" << kSchemeName
            << ",operation=" << operation
            << ",size=" << size
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << elapsed_ms
            << ",ops_per_sec=" << ops_per_sec
            << ",values_per_sec=0"
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

        const auto rows = hebench::read_exact_csv(args.corpus_path);
        if (rows.empty())
        {
            throw std::runtime_error("corpus has no rows: " + args.corpus_path);
        }
        if (rows.size() > args.ring_size)
        {
            throw std::runtime_error("corpus row count exceeds OpenFHE exact batch size");
        }
        const auto baseline_kb = hebench::peak_rss_kb();

        const hebench::Timer context_timer;
#ifdef HE_BENCHMARK_OPENFHE_MEMORY_BGV
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
        plain_a = crypto_context->MakePackedPlaintext(encode_signed_inputs(rows, false, args.ring_size, kPlainModulus));
        plain_b = crypto_context->MakePackedPlaintext(encode_signed_inputs(rows, true, args.ring_size, kPlainModulus));
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
        auto decoded = decrypt_decode(crypto_context, keys.secretKey, product, kPlainModulus);
        std::string error;
        auto correct = hebench::compare_exact_slots(decoded, rows, ExactOperation::mul, kPlainModulus, error);
        print_row(
            "memory_multiply",
            rows.size(),
            args.ring_size,
            correct,
            multiply_ms,
            baseline_kb,
            "components_after=" + std::to_string(product->NumberCiphertextElements()) +
                (correct ? "" : ",error=\"" + error + "\""));

        const hebench::Timer rotate_timer;
        const auto rotated = crypto_context->EvalRotate(encrypted_a, 1);
        const auto rotate_ms = rotate_timer.elapsed_ms();
        print_row("memory_rotate", rows.size(), args.ring_size, rotated != nullptr, rotate_ms, baseline_kb, "rotation_step=1");

        const hebench::Timer decrypt_timer;
        decoded = decrypt_decode(crypto_context, keys.secretKey, encrypted_a, kPlainModulus);
        const auto decrypt_ms = decrypt_timer.elapsed_ms();
        print_row("memory_decrypt", rows.size(), args.ring_size, decoded.size() >= rows.size(), decrypt_ms, baseline_kb);

        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << kBinaryName << " failed: " << error.what() << '\n';
        return 2;
    }
}
