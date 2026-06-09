#include <cstdint>
#include <algorithm>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "seal/seal.h"

#include "benchmark_args.hpp"
#include "csv_reader.hpp"
#include "exact_compare.hpp"
#include "timer.hpp"

namespace
{
    constexpr std::uint64_t kPlainModulusBitSize = 20;
#ifdef HE_BENCHMARK_SEAL_ROTATION_BGV
    constexpr auto kScheme = seal::scheme_type::bgv;
    constexpr const char *kSchemeName = "BGV";
    constexpr const char *kBinaryName = "seal_bgv_rotation";
#else
    constexpr auto kScheme = seal::scheme_type::bfv;
    constexpr const char *kSchemeName = "BFV";
    constexpr const char *kBinaryName = "seal_bfv_rotation";
#endif

    std::vector<std::uint64_t> encode_rotation_inputs(
        const std::vector<hebench::RotationRow> &rows,
        std::size_t slot_count,
        std::uint64_t plain_modulus)
    {
        std::vector<std::uint64_t> values(slot_count, 0ULL);
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto centered = hebench::centered_mod(rows[i].value, plain_modulus);
            values[i] = centered < 0
                ? static_cast<std::uint64_t>(centered + static_cast<std::int64_t>(plain_modulus))
                : static_cast<std::uint64_t>(centered);
        }
        return values;
    }

    template <typename T>
    std::size_t serialized_size(const T &value)
    {
        std::stringstream stream;
        value.save(stream);
        return stream.str().size();
    }

    std::vector<std::uint64_t> decrypt_decode(
        seal::Decryptor &decryptor,
        seal::BatchEncoder &encoder,
        const seal::Ciphertext &ciphertext)
    {
        seal::Plaintext plain;
        std::vector<std::uint64_t> decoded;
        decryptor.decrypt(ciphertext, plain);
        encoder.decode(plain, decoded);
        return decoded;
    }

    std::int64_t expected_rowwise(
        const std::vector<hebench::RotationRow> &rows,
        std::size_t index,
        std::size_t row_size,
        int step,
        bool left)
    {
        const auto row_start = (index / row_size) * row_size;
        const auto position = index - row_start;
        const auto normalized_step = static_cast<std::size_t>(
            ((step % static_cast<int>(row_size)) + static_cast<int>(row_size)) %
            static_cast<int>(row_size));
        const auto source_position = left
            ? (position + normalized_step) % row_size
            : (position + row_size - normalized_step) % row_size;
        const auto source_index = row_start + source_position;
        return source_index < rows.size() ? rows[source_index].value : 0;
    }

    bool compare_rotation(
        const std::vector<std::uint64_t> &actual,
        const std::vector<hebench::RotationRow> &rows,
        std::size_t row_size,
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
                const auto expected = hebench::centered_mod(expected_rowwise(rows, i, row_size, step, left), plain_modulus);
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
            const auto expected = hebench::centered_mod(expected_rowwise(rows, i, row_size, step, true), plain_modulus);
            const auto decoded = hebench::centered_mod_u64(actual[i], plain_modulus);
            if (decoded != expected)
            {
                error = "rotation output did not match left or right row-wise rotation; slot " +
                    std::to_string(i) + " expected-left " + std::to_string(expected) +
                    " got " + std::to_string(decoded);
                return false;
            }
        }

        error = "rotation output did not match left or right row-wise rotation";
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
        std::size_t byte_size,
        const std::string &error = "")
    {
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto values_per_sec = elapsed_ms > 0.0 ? static_cast<double>(values_count) * 1000.0 / elapsed_ms : 0.0;

        std::cout
            << "library=SEAL"
            << ",scheme=" << kSchemeName
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
        if (byte_size > 0)
        {
            std::cout << ",byte_size=" << byte_size;
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

        seal::EncryptionParameters parms(kScheme);
        parms.set_poly_modulus_degree(args.ring_size);
        parms.set_coeff_modulus(seal::CoeffModulus::BFVDefault(args.ring_size));
        parms.set_plain_modulus(seal::PlainModulus::Batching(args.ring_size, kPlainModulusBitSize));

        seal::SEALContext context(parms);
        if (!context.parameters_set())
        {
            throw std::runtime_error(context.parameter_error_message());
        }

        seal::BatchEncoder encoder(context);
        const auto slot_count = encoder.slot_count();
        const auto row_size = slot_count / 2;
        const auto plain_modulus = parms.plain_modulus().value();
        if (rows.size() > slot_count)
        {
            throw std::runtime_error(std::string("corpus row count exceeds SEAL ") + kSchemeName + " slot count");
        }

        seal::KeyGenerator keygen(context);
        const auto secret_key = keygen.secret_key();
        seal::PublicKey public_key;
        keygen.create_public_key(public_key);

        seal::GaloisKeys galois_keys;
        const hebench::Timer key_timer;
        keygen.create_galois_keys(std::vector<int>{1, -1, 8}, galois_keys);
        const auto key_ms = key_timer.elapsed_ms();
        print_row("rotation_keygen", rows.size(), args.ring_size, 0, true, key_ms, 0, "", serialized_size(galois_keys));

        seal::Encryptor encryptor(context, public_key);
        seal::Evaluator evaluator(context);
        seal::Decryptor decryptor(context, secret_key);

        seal::Plaintext plain;
        encoder.encode(encode_rotation_inputs(rows, slot_count, plain_modulus), plain);
        seal::Ciphertext encrypted;
        encryptor.encrypt(plain, encrypted);

        bool all_correct = true;
        for (const auto step : {1, -1, 8})
        {
            seal::Ciphertext result;
            const hebench::Timer timer;
            evaluator.rotate_rows(encrypted, step, galois_keys, result);
            const auto elapsed_ms = timer.elapsed_ms();
            const auto decoded = decrypt_decode(decryptor, encoder, result);

            std::string matched_direction;
            std::string error;
            const bool correct = compare_rotation(
                decoded,
                rows,
                row_size,
                step,
                plain_modulus,
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
                serialized_size(result),
                error);
            all_correct = correct && all_correct;
        }

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << kBinaryName << " failed: " << error.what() << '\n';
        return 2;
    }
}
