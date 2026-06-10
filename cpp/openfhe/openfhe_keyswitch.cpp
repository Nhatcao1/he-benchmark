#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "openfhe.h"

#include "benchmark_args.hpp"
#include "csv_reader.hpp"
#include "exact_compare.hpp"
#include "timer.hpp"

namespace
{
    constexpr std::uint64_t kPlainModulus = 786433;

#ifdef HE_BENCHMARK_OPENFHE_KEYSWITCH_CKKS
    constexpr const char *kSchemeName = "CKKS";
    constexpr const char *kBinaryName = "openfhe_ckks_keyswitch";
#elif defined(HE_BENCHMARK_OPENFHE_KEYSWITCH_BGV)
    constexpr const char *kSchemeName = "BGV";
    constexpr const char *kBinaryName = "openfhe_bgv_keyswitch";
#else
    constexpr const char *kSchemeName = "BFV";
    constexpr const char *kBinaryName = "openfhe_bfv_keyswitch";
#endif

#ifdef HE_BENCHMARK_OPENFHE_KEYSWITCH_CKKS
    std::vector<double> ckks_values(
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
#else
    std::vector<int64_t> exact_values(
        const std::vector<hebench::ExactRow> &rows,
        std::size_t slot_count)
    {
        std::vector<int64_t> values(slot_count, 0);
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            values[i] = hebench::centered_mod(rows[i].a, kPlainModulus);
        }
        return values;
    }
#endif

    void print_row(
        const std::string &operation,
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        double elapsed_ms,
        const std::string &extra = "")
    {
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto values_per_sec = elapsed_ms > 0.0 ? static_cast<double>(size) * 1000.0 / elapsed_ms : 0.0;
        std::cout
            << "library=OpenFHE"
            << ",scheme=" << kSchemeName
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

#ifdef HE_BENCHMARK_OPENFHE_KEYSWITCH_CKKS
        const auto rows = hebench::read_ckks_csv(args.corpus_path);
        if (rows.empty())
        {
            throw std::runtime_error("corpus has no rows: " + args.corpus_path);
        }
        lbcrypto::CCParams<lbcrypto::CryptoContextCKKSRNS> parameters;
        parameters.SetMultiplicativeDepth(2);
        parameters.SetScalingModSize(50);
        parameters.SetFirstModSize(60);
        parameters.SetSecurityLevel(lbcrypto::HEStd_128_classic);
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size / 2));
        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);

        auto old_keys = crypto_context->KeyGen();
        auto new_keys = crypto_context->KeyGen();
        const hebench::Timer keygen_timer;
        auto switch_key = crypto_context->KeySwitchGen(old_keys.secretKey, new_keys.secretKey);
        const auto keygen_ms = keygen_timer.elapsed_ms();
        print_row("key_switch_keygen", rows.size(), args.ring_size, true, keygen_ms);

        auto plaintext = crypto_context->MakeCKKSPackedPlaintext(ckks_values(rows, args.ring_size / 2));
        auto encrypted = crypto_context->Encrypt(old_keys.publicKey, plaintext);
        auto key_switch_input = encrypted;
        const hebench::Timer switch_timer;
        auto switched = crypto_context->KeySwitch(key_switch_input, switch_key);
        const auto switch_ms = switch_timer.elapsed_ms();
        lbcrypto::Plaintext decrypted;
        crypto_context->Decrypt(new_keys.secretKey, switched, &decrypted);
        decrypted->SetLength(rows.size());
        const auto decoded = decrypted->GetRealPackedValue();
        double max_error = 0.0;
        for (std::size_t i = 0; i < rows.size() && i < decoded.size(); ++i)
        {
            max_error = std::max(max_error, std::abs(decoded[i] - rows[i].a));
        }
        print_row(
            "key_switch_apply",
            rows.size(),
            args.ring_size,
            max_error <= 1e-3,
            switch_ms,
            "max_abs_error=" + std::to_string(max_error));
#else
        const auto rows = hebench::read_exact_csv(args.corpus_path);
        if (rows.empty())
        {
            throw std::runtime_error("corpus has no rows: " + args.corpus_path);
        }
#ifdef HE_BENCHMARK_OPENFHE_KEYSWITCH_BGV
        lbcrypto::CCParams<lbcrypto::CryptoContextBGVRNS> parameters;
#else
        lbcrypto::CCParams<lbcrypto::CryptoContextBFVRNS> parameters;
#endif
        parameters.SetPlaintextModulus(kPlainModulus);
        parameters.SetMultiplicativeDepth(2);
        parameters.SetSecurityLevel(lbcrypto::HEStd_128_classic);
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size));
        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);

        auto old_keys = crypto_context->KeyGen();
        auto new_keys = crypto_context->KeyGen();
        const hebench::Timer keygen_timer;
        auto switch_key = crypto_context->KeySwitchGen(old_keys.secretKey, new_keys.secretKey);
        const auto keygen_ms = keygen_timer.elapsed_ms();
        print_row("key_switch_keygen", rows.size(), args.ring_size, true, keygen_ms);

        auto plaintext = crypto_context->MakePackedPlaintext(exact_values(rows, args.ring_size));
        auto encrypted = crypto_context->Encrypt(old_keys.publicKey, plaintext);
        auto key_switch_input = encrypted;
        const hebench::Timer switch_timer;
        auto switched = crypto_context->KeySwitch(key_switch_input, switch_key);
        const auto switch_ms = switch_timer.elapsed_ms();

        lbcrypto::Plaintext decrypted;
        crypto_context->Decrypt(new_keys.secretKey, switched, &decrypted);
        const auto &packed = decrypted->GetPackedValue();
        bool correct = packed.size() >= rows.size();
        for (std::size_t i = 0; correct && i < rows.size(); ++i)
        {
            correct = hebench::centered_mod(packed[i], kPlainModulus) ==
                hebench::centered_mod(rows[i].a, kPlainModulus);
        }
        print_row("key_switch_apply", rows.size(), args.ring_size, correct, switch_ms);
#endif
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << kBinaryName << " failed: " << error.what() << '\n';
        return 2;
    }
}
