#include <cstdint>
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

#ifdef HE_BENCHMARK_SEAL_DOT_BGV
    constexpr auto kScheme = seal::scheme_type::bgv;
    constexpr const char *kSchemeName = "BGV";
    constexpr const char *kBinaryName = "seal_bgv_dot";
#else
    constexpr auto kScheme = seal::scheme_type::bfv;
    constexpr const char *kSchemeName = "BFV";
    constexpr const char *kBinaryName = "seal_bfv_dot";
#endif

    template <typename T>
    std::size_t serialized_size(const T &value)
    {
        std::stringstream stream;
        value.save(stream);
        return stream.str().size();
    }

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

    std::vector<std::uint64_t> encode_signed_inputs(
        const std::vector<hebench::ExactRow> &rows,
        bool use_b,
        std::size_t slot_count,
        std::uint64_t plain_modulus)
    {
        std::vector<std::uint64_t> values(slot_count, 0ULL);
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto value = use_b ? rows[i].b : rows[i].a;
            const auto centered = hebench::centered_mod(value, plain_modulus);
            values[i] = centered < 0
                ? static_cast<std::uint64_t>(centered + static_cast<std::int64_t>(plain_modulus))
                : static_cast<std::uint64_t>(centered);
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

    void print_row(
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        double elapsed_ms,
        std::size_t byte_size,
        std::size_t rotations_count,
        std::size_t components_after,
        std::int64_t expected,
        std::int64_t actual,
        const std::string &error = "")
    {
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto values_per_sec = elapsed_ms > 0.0 ? static_cast<double>(size) * 1000.0 / elapsed_ms : 0.0;

        std::cout
            << "library=SEAL"
            << ",scheme=" << kSchemeName
            << ",operation=dot_product_e2e"
            << ",size=" << size
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << elapsed_ms
            << ",ops_per_sec=" << ops_per_sec
            << ",values_per_sec=" << values_per_sec
            << ",byte_size=" << byte_size
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
        if (rows.size() > row_size)
        {
            throw std::runtime_error("dot workload row count exceeds one SEAL batching row");
        }

        seal::KeyGenerator keygen(context);
        const auto secret_key = keygen.secret_key();
        seal::PublicKey public_key;
        seal::RelinKeys relin_keys;
        seal::GaloisKeys galois_keys;
        keygen.create_public_key(public_key);
        keygen.create_relin_keys(relin_keys);
        keygen.create_galois_keys(rotation_steps(rows.size()), galois_keys);

        seal::Encryptor encryptor(context, public_key);
        seal::Evaluator evaluator(context);
        seal::Decryptor decryptor(context, secret_key);

        seal::Ciphertext result;
        std::vector<std::uint64_t> decoded;
        const auto expected = expected_dot(rows, plain_modulus);

        const hebench::Timer timer;
        seal::Plaintext plain_a;
        seal::Plaintext plain_b;
        encoder.encode(encode_signed_inputs(rows, false, slot_count, plain_modulus), plain_a);
        encoder.encode(encode_signed_inputs(rows, true, slot_count, plain_modulus), plain_b);

        seal::Ciphertext encrypted_a;
        seal::Ciphertext encrypted_b;
        encryptor.encrypt(plain_a, encrypted_a);
        encryptor.encrypt(plain_b, encrypted_b);

        evaluator.multiply(encrypted_a, encrypted_b, result);
        evaluator.relinearize_inplace(result, relin_keys);
        for (std::size_t step = 1; step < rows.size(); step *= 2)
        {
            seal::Ciphertext rotated;
            evaluator.rotate_rows(result, static_cast<int>(step), galois_keys, rotated);
            evaluator.add_inplace(result, rotated);
        }

        seal::Plaintext dot_plain;
        decryptor.decrypt(result, dot_plain);
        encoder.decode(dot_plain, decoded);
        const auto elapsed_ms = timer.elapsed_ms();

        const auto actual = decoded.empty() ? 0 : hebench::centered_mod_u64(decoded[0], plain_modulus);
        const bool correct = actual == expected;
        const std::string error = correct
            ? ""
            : "expected " + std::to_string(expected) + " got " + std::to_string(actual);

        print_row(
            rows.size(),
            args.ring_size,
            correct,
            elapsed_ms,
            serialized_size(result),
            rotation_steps(rows.size()).size(),
            result.size(),
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
