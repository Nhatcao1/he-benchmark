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
#include "csv_reader.hpp"
#include "timer.hpp"

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
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        double elapsed_ms,
        std::size_t rotations_count,
        std::size_t components_after,
        double scale_after,
        std::size_t level_after,
        double expected,
        double actual)
    {
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto values_per_sec = elapsed_ms > 0.0 ? static_cast<double>(size) * 1000.0 / elapsed_ms : 0.0;
        const auto abs_error = std::abs(actual - expected);
        const auto relative_error = abs_error / std::max(std::abs(expected), 1e-12);
        const auto precision_bits = relative_error > 0.0 ? -std::log2(relative_error) : INFINITY;

        std::cout
            << "library=OpenFHE"
            << ",scheme=CKKS"
            << ",operation=dot_product_e2e"
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
        std::cout
            << ",rotations_count=" << rotations_count
            << ",components_after=" << components_after
            << ",scale_after=" << scale_after
            << ",level_after=" << level_after
            << ",expected=" << expected
            << ",actual=" << actual
            << ",abs_error=" << abs_error
            << ",relative_error=" << relative_error
            << ",precision_bits=" << precision_bits;
        if (!correct)
        {
            std::cout << ",error=\"expected " << expected << " got " << actual << "\"";
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
        if (!is_power_of_two(rows.size()))
        {
            throw std::runtime_error("dot workload requires a power-of-two row count");
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

        auto keys = crypto_context->KeyGen();
        crypto_context->EvalMultKeyGen(keys.secretKey);
        crypto_context->EvalRotateKeyGen(keys.secretKey, rotation_steps(rows.size()));

        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> result;
        const auto expected = expected_dot(rows);

        const hebench::Timer timer;
        const auto plain_a = crypto_context->MakeCKKSPackedPlaintext(values_for(rows, false, args.ring_size / 2));
        const auto plain_b = crypto_context->MakeCKKSPackedPlaintext(values_for(rows, true, args.ring_size / 2));
        const auto encrypted_a = crypto_context->Encrypt(keys.publicKey, plain_a);
        const auto encrypted_b = crypto_context->Encrypt(keys.publicKey, plain_b);

        result = crypto_context->EvalMult(encrypted_a, encrypted_b);
        for (const auto step : rotation_steps(rows.size()))
        {
            const auto rotated = crypto_context->EvalRotate(result, step);
            result = crypto_context->EvalAdd(result, rotated);
        }

        const auto decoded = decrypt_decode(crypto_context, keys.secretKey, result, rows.size());
        const auto elapsed_ms = timer.elapsed_ms();

        const auto actual = decoded.empty() ? 0.0 : decoded[0];
        const auto abs_error = std::abs(actual - expected);
        const auto relative_error = abs_error / std::max(std::abs(expected), 1e-12);
        const bool correct = abs_error <= 1e-3 || relative_error <= 1e-3;

        print_row(
            rows.size(),
            args.ring_size,
            correct,
            elapsed_ms,
            rotation_steps(rows.size()).size(),
            result->NumberCiphertextElements(),
            result->GetScalingFactor(),
            result->GetLevel(),
            expected,
            actual);

        return correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "openfhe_ckks_dot failed: " << error.what() << '\n';
        return 2;
    }
}
