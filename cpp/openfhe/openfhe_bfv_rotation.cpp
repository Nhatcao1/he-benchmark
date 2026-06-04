#include <cstdint>
#include <algorithm>
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

    std::vector<int64_t> encode_rotation_inputs(
        const std::vector<hebench::RotationRow> &rows,
        std::size_t slot_count,
        std::uint64_t plain_modulus)
    {
        std::vector<int64_t> values(slot_count, 0);
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            values[i] = hebench::centered_mod(rows[i].value, plain_modulus);
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

    std::int64_t expected_full_vector(
        const std::vector<hebench::RotationRow> &rows,
        std::size_t index,
        std::size_t rotation_span,
        int step,
        bool left)
    {
        const auto normalized_step = static_cast<std::size_t>(
            ((step % static_cast<int>(rotation_span)) + static_cast<int>(rotation_span)) %
            static_cast<int>(rotation_span));
        const auto source_position = left
            ? (index + normalized_step) % rotation_span
            : (index + rotation_span - normalized_step) % rotation_span;
        return source_position < rows.size() ? rows[source_position].value : 0;
    }

    bool compare_rotation(
        const std::vector<std::uint64_t> &actual,
        const std::vector<hebench::RotationRow> &rows,
        std::size_t rotation_span,
        int step,
        std::uint64_t plain_modulus,
        std::string &matched_direction,
        std::string &error)
    {
        if (actual.size() < rows.size())
        {
            error = "decoded slot count is smaller than corpus row count";
            return false;
        }

        for (const auto left : {true, false})
        {
            bool ok = true;
            for (std::size_t i = 0; i < rows.size(); ++i)
            {
                const auto expected = hebench::centered_mod(
                    expected_full_vector(rows, i, rotation_span, step, left),
                    plain_modulus);
                const auto decoded = hebench::centered_mod_u64(actual[i], plain_modulus);
                if (decoded != expected)
                {
                    ok = false;
                    break;
                }
            }
            if (ok)
            {
                matched_direction = left ? "left" : "right";
                error.clear();
                return true;
            }
        }

        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto expected = hebench::centered_mod(
                expected_full_vector(rows, i, rotation_span, step, true),
                plain_modulus);
            const auto decoded = hebench::centered_mod_u64(actual[i], plain_modulus);
            if (decoded != expected)
            {
                error = "rotation output did not match left or right full-vector rotation; slot " +
                    std::to_string(i) + " expected-left " + std::to_string(expected) +
                    " got " + std::to_string(decoded);
                return false;
            }
        }

        error = "rotation output did not match left or right full-vector rotation";
        return false;
    }

    void print_row(
        const std::string &operation,
        std::size_t size,
        std::size_t ring_size,
        int step,
        bool correct,
        double elapsed_ms,
        std::size_t values_count,
        const std::string &matched_direction,
        const std::string &error = "")
    {
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto values_per_sec = elapsed_ms > 0.0 ? static_cast<double>(values_count) * 1000.0 / elapsed_ms : 0.0;

        std::cout
            << "library=OpenFHE"
            << ",scheme=BFV"
            << ",operation=" << operation
            << ",size=" << size
            << ",ring_size=" << ring_size
            << ",rotation_step=" << step
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << elapsed_ms
            << ",ops_per_sec=" << ops_per_sec
            << ",values_per_sec=" << values_per_sec;
        if (!matched_direction.empty())
        {
            std::cout << ",matched_direction=" << matched_direction;
        }
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

        const auto rows = hebench::read_rotation_csv(args.corpus_path);
        if (rows.empty())
        {
            throw std::runtime_error("corpus has no rows: " + args.corpus_path);
        }
        if (rows.size() > args.ring_size)
        {
            throw std::runtime_error("corpus row count exceeds OpenFHE BFV batch size");
        }

        lbcrypto::CCParams<lbcrypto::CryptoContextBFVRNS> parameters;
        parameters.SetPlaintextModulus(kPlainModulus);
        parameters.SetMultiplicativeDepth(kMultiplicativeDepth);
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size));

        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);
        crypto_context->Enable(lbcrypto::ADVANCEDSHE);

        auto keys = crypto_context->KeyGen();

        const hebench::Timer key_timer;
        crypto_context->EvalRotateKeyGen(keys.secretKey, {1, -1, 8});
        const auto key_ms = key_timer.elapsed_ms();
        print_row("rotation_keygen", rows.size(), args.ring_size, 0, true, key_ms, 0, "");

        const auto plain = crypto_context->MakePackedPlaintext(
            encode_rotation_inputs(rows, args.ring_size, kPlainModulus));
        const auto encrypted = crypto_context->Encrypt(keys.publicKey, plain);
        const auto rotation_span = std::max(rows.size(), args.ring_size / 2);

        bool all_correct = true;
        for (const auto step : {1, -1, 8})
        {
            const hebench::Timer timer;
            const auto result = crypto_context->EvalRotate(encrypted, step);
            const auto elapsed_ms = timer.elapsed_ms();
            const auto decoded = decrypt_decode(crypto_context, keys.secretKey, result, kPlainModulus);

            std::string matched_direction;
            std::string error;
            const bool correct = compare_rotation(
                decoded,
                rows,
                rotation_span,
                step,
                kPlainModulus,
                matched_direction,
                error);
            print_row(
                "rotate_" + std::to_string(step),
                rows.size(),
                args.ring_size,
                step,
                correct,
                elapsed_ms,
                rows.size(),
                matched_direction,
                error);
            all_correct = correct && all_correct;
        }

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "openfhe_bfv_rotation failed: " << error.what() << '\n';
        return 2;
    }
}
