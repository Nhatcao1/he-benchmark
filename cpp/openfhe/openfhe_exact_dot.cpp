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
    constexpr std::uint32_t kMultiplicativeDepth = 2;

#ifdef HE_BENCHMARK_OPENFHE_DOT_BGV
    constexpr const char *kSchemeName = "BGV";
    constexpr const char *kBinaryName = "openfhe_bgv_dot";
#else
    constexpr const char *kSchemeName = "BFV";
    constexpr const char *kBinaryName = "openfhe_bfv_dot";
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

    std::int64_t expected_dot(
        const std::vector<hebench::ExactRow> &rows,
        std::uint64_t plain_modulus)
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
            const auto value = use_b ? rows[i].b : rows[i].a;
            values[i] = hebench::centered_mod(value, plain_modulus);
        }
        return values;
    }

    std::vector<int> rotation_steps(std::size_t size)
    {
        std::vector<int> steps;
        for (std::size_t step = 1; step < size; step *= 2)
        {
            steps.push_back(static_cast<int>(step));
        }
        return steps;
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
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        double elapsed_ms,
        std::size_t rotations_count,
        std::size_t components_after,
        std::int64_t expected,
        std::int64_t actual,
        const std::string &error = "")
    {
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto values_per_sec = elapsed_ms > 0.0 ? static_cast<double>(size) * 1000.0 / elapsed_ms : 0.0;

        std::cout
            << "library=OpenFHE"
            << ",scheme=" << kSchemeName
            << ",operation=dot_product_e2e"
            << ",size=" << size
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << elapsed_ms
            << ",ops_per_sec=" << ops_per_sec
            << ",values_per_sec=" << values_per_sec
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
            throw std::runtime_error("dot workload requires a power-of-two row count");
        }
        if (rows.size() > args.ring_size / 2)
        {
            throw std::runtime_error("dot workload row count exceeds conservative OpenFHE exact slot budget");
        }

#ifdef HE_BENCHMARK_OPENFHE_DOT_BGV
        lbcrypto::CCParams<lbcrypto::CryptoContextBGVRNS> parameters;
#else
        lbcrypto::CCParams<lbcrypto::CryptoContextBFVRNS> parameters;
#endif
        parameters.SetPlaintextModulus(kPlainModulus);
        parameters.SetMultiplicativeDepth(kMultiplicativeDepth);
        parameters.SetSecurityLevel(lbcrypto::HEStd_128_classic);
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size));

        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);
        crypto_context->Enable(lbcrypto::ADVANCEDSHE);

        auto keys = crypto_context->KeyGen();
        crypto_context->EvalMultKeyGen(keys.secretKey);
        crypto_context->EvalRotateKeyGen(keys.secretKey, rotation_steps(rows.size()));

        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> result;
        const auto expected = expected_dot(rows, kPlainModulus);

        const hebench::Timer timer;
        const auto plain_a = crypto_context->MakePackedPlaintext(
            encode_signed_inputs(rows, false, args.ring_size, kPlainModulus));
        const auto plain_b = crypto_context->MakePackedPlaintext(
            encode_signed_inputs(rows, true, args.ring_size, kPlainModulus));
        const auto encrypted_a = crypto_context->Encrypt(keys.publicKey, plain_a);
        const auto encrypted_b = crypto_context->Encrypt(keys.publicKey, plain_b);

        result = crypto_context->EvalMult(encrypted_a, encrypted_b);
        for (const auto step : rotation_steps(rows.size()))
        {
            const auto rotated = crypto_context->EvalRotate(result, step);
            result = crypto_context->EvalAdd(result, rotated);
        }

        const auto decoded = decrypt_decode(crypto_context, keys.secretKey, result, kPlainModulus);
        const auto elapsed_ms = timer.elapsed_ms();

        const auto actual = decoded.empty() ? 0 : hebench::centered_mod_u64(decoded[0], kPlainModulus);
        const bool correct = actual == expected;
        const std::string error = correct
            ? ""
            : "expected " + std::to_string(expected) + " got " + std::to_string(actual);

        print_row(
            rows.size(),
            args.ring_size,
            correct,
            elapsed_ms,
            rotation_steps(rows.size()).size(),
            result->NumberCiphertextElements(),
            expected,
            actual,
            error);

        return correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << kBinaryName << " failed: " << error.what() << '\n';
        return 2;
    }
}
