#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "seal/seal.h"

#include "benchmark_args.hpp"
#include "csv_reader.hpp"
#include "timer.hpp"

namespace
{
    constexpr double kScale = static_cast<double>(std::uint64_t{1} << 40);

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
            << "library=SEAL"
            << ",scheme=CKKS"
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

        seal::EncryptionParameters parms(seal::scheme_type::ckks);
        parms.set_poly_modulus_degree(args.ring_size);
        parms.set_coeff_modulus(seal::CoeffModulus::Create(args.ring_size, {60, 40, 40, 60}));

        seal::SEALContext context(parms);
        if (!context.parameters_set())
        {
            throw std::runtime_error(context.parameter_error_message());
        }

        seal::CKKSEncoder encoder(context);
        const auto slot_count = encoder.slot_count();
        if (rows.size() > slot_count)
        {
            throw std::runtime_error("corpus row count exceeds SEAL CKKS slot count");
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
        std::vector<double> decoded;
        const auto expected = expected_dot(rows);

        const hebench::Timer timer;
        seal::Plaintext plain_a;
        seal::Plaintext plain_b;
        encoder.encode(values_for(rows, false, slot_count), kScale, plain_a);
        encoder.encode(values_for(rows, true, slot_count), kScale, plain_b);

        seal::Ciphertext encrypted_a;
        seal::Ciphertext encrypted_b;
        encryptor.encrypt(plain_a, encrypted_a);
        encryptor.encrypt(plain_b, encrypted_b);

        evaluator.multiply(encrypted_a, encrypted_b, result);
        evaluator.relinearize_inplace(result, relin_keys);
        evaluator.rescale_to_next_inplace(result);
        for (std::size_t step = 1; step < rows.size(); step *= 2)
        {
            seal::Ciphertext rotated;
            evaluator.rotate_vector(result, static_cast<int>(step), galois_keys, rotated);
            evaluator.add_inplace(result, rotated);
        }

        seal::Plaintext dot_plain;
        decryptor.decrypt(result, dot_plain);
        encoder.decode(dot_plain, decoded);
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
            serialized_size(result),
            rotation_steps(rows.size()).size(),
            result.size(),
            result.scale(),
            result.coeff_modulus_size(),
            expected,
            actual);

        return correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "seal_ckks_dot failed: " << error.what() << '\n';
        return 2;
    }
}
