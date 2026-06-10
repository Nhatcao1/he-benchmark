#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "openfhe.h"

#include "benchmark_args.hpp"
#include "csv_reader.hpp"
#include "depth_compare.hpp"
#include "timer.hpp"

namespace
{
    constexpr std::uint64_t kPlainModulus = 786433;

#ifdef HE_BENCHMARK_OPENFHE_DEPTH_BGV
    constexpr const char *kSchemeName = "BGV";
    constexpr const char *kBinaryName = "openfhe_bgv_depth";
#else
    constexpr const char *kSchemeName = "BFV";
    constexpr const char *kBinaryName = "openfhe_bfv_depth";
#endif

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

    std::vector<int64_t> encode_signed_inputs(
        const std::vector<hebench::ExactDepthRow> &rows,
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

    std::size_t corpus_depth_count(const std::vector<hebench::ExactDepthRow> &rows)
    {
        const auto count = rows.front().expected_depth.size();
        for (const auto &row : rows)
        {
            if (row.expected_depth.size() != count)
            {
                throw std::runtime_error("inconsistent exact depth column count in corpus");
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

        const auto rows = hebench::read_exact_depth_csv(args.corpus_path);
        if (rows.empty())
        {
            throw std::runtime_error("corpus has no rows: " + args.corpus_path);
        }
        if (rows.size() > args.ring_size)
        {
            throw std::runtime_error("corpus row count exceeds OpenFHE exact batch size");
        }
        const auto max_depth = std::min(args.max_depth, corpus_depth_count(rows));

#ifdef HE_BENCHMARK_OPENFHE_DEPTH_BGV
        lbcrypto::CCParams<lbcrypto::CryptoContextBGVRNS> parameters;
#else
        lbcrypto::CCParams<lbcrypto::CryptoContextBFVRNS> parameters;
#endif
        parameters.SetPlaintextModulus(kPlainModulus);
        parameters.SetMultiplicativeDepth(static_cast<std::uint32_t>(max_depth));
        parameters.SetSecurityLevel(lbcrypto::HEStd_128_classic);
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size));

        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);

        auto keys = crypto_context->KeyGen();
        crypto_context->EvalMultKeyGen(keys.secretKey);

        const auto plain_a = crypto_context->MakePackedPlaintext(
            encode_signed_inputs(rows, false, args.ring_size, kPlainModulus));
        const auto plain_b = crypto_context->MakePackedPlaintext(
            encode_signed_inputs(rows, true, args.ring_size, kPlainModulus));
        const auto encrypted_a = crypto_context->Encrypt(keys.publicKey, plain_a);
        const auto encrypted_b = crypto_context->Encrypt(keys.publicKey, plain_b);

        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> current;
        bool keep_running = true;
        for (std::size_t depth = 1; depth <= max_depth && keep_running; ++depth)
        {
            const auto left = depth == 1 ? encrypted_a : current;
            const auto right = depth == 1 ? encrypted_b : current;
            const auto level_before = left->GetLevel();

            const hebench::Timer timer;
            const auto next = crypto_context->EvalMult(left, right);
            const auto elapsed_ms = timer.elapsed_ms();

            const auto decoded = decrypt_decode(crypto_context, keys.secretKey, next, kPlainModulus);
            const auto expected = hebench::exact_depth_expected(rows, depth, kPlainModulus);
            std::string error;
            const bool correct = hebench::compare_exact_depth_slots(decoded, expected, kPlainModulus, error);

            print_metric_row(
                "depth_mul",
                rows.size(),
                args.ring_size,
                correct,
                elapsed_ms,
                rows.size(),
                "depth=" + std::to_string(depth) +
                    ",max_depth=" + std::to_string(max_depth) +
                    ",security=HEStd_128_classic" +
                    ",level_before=" + std::to_string(level_before) +
                    ",level_after=" + std::to_string(next->GetLevel()) +
                    ",components_after=" + std::to_string(next->NumberCiphertextElements()) +
                    (correct ? "" : ",error=\"" + error + "\""));

            current = next;
            keep_running = correct;
        }

        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << kBinaryName << " failed: " << error.what() << '\n';
        return 2;
    }
}
