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
#ifdef HE_BENCHMARK_SEAL_MATRIX_BGV
    constexpr const char *kSchemeName = "BGV";
    constexpr seal::scheme_type kSchemeType = seal::scheme_type::bgv;
    constexpr const char *kBinaryName = "seal_bgv_matrix";
#else
    constexpr const char *kSchemeName = "BFV";
    constexpr seal::scheme_type kSchemeType = seal::scheme_type::bfv;
    constexpr const char *kBinaryName = "seal_bfv_matrix";
#endif

    constexpr std::uint64_t kPlainModulus = 786433;

    template <typename T>
    std::size_t serialized_size(const T &value)
    {
        std::stringstream stream;
        value.save(stream);
        return stream.str().size();
    }

    std::string sibling_matrix_path(const std::string &matrix_a_path, const std::string &filename)
    {
        const auto slash = matrix_a_path.find_last_of("/\\");
        if (slash == std::string::npos)
        {
            return filename;
        }
        return matrix_a_path.substr(0, slash + 1) + filename;
    }

    std::uint64_t normalize_exact(double value)
    {
        const auto rounded = static_cast<long long>(std::llround(value));
        const auto mod = static_cast<long long>(kPlainModulus);
        auto normalized = rounded % mod;
        if (normalized < 0)
        {
            normalized += mod;
        }
        return static_cast<std::uint64_t>(normalized);
    }

    std::vector<std::uint64_t> matrix_row(
        const hebench::DenseMatrix &matrix,
        std::size_t row,
        std::size_t slot_count)
    {
        std::vector<std::uint64_t> values(slot_count, 0);
        for (std::size_t col = 0; col < matrix.cols; ++col)
        {
            values[col] = normalize_exact(matrix.at(row, col));
        }
        return values;
    }

    std::vector<std::uint64_t> matrix_col(
        const hebench::DenseMatrix &matrix,
        std::size_t col,
        std::size_t slot_count)
    {
        std::vector<std::uint64_t> values(slot_count, 0);
        for (std::size_t row = 0; row < matrix.rows; ++row)
        {
            values[row] = normalize_exact(matrix.at(row, col));
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

    std::uint64_t expected_exact_cell(
        const hebench::DenseMatrix &matrix_a,
        const hebench::DenseMatrix &matrix_b,
        std::size_t row,
        std::size_t col)
    {
        std::uint64_t total = 0;
        for (std::size_t k = 0; k < matrix_a.cols; ++k)
        {
            const auto a = normalize_exact(matrix_a.at(row, k));
            const auto b = normalize_exact(matrix_b.at(k, col));
            total = (total + (a * b) % kPlainModulus) % kPlainModulus;
        }
        return total;
    }

    void print_row(
        std::size_t dimension,
        std::size_t ring_size,
        bool correct,
        double elapsed_ms,
        std::size_t byte_size,
        const std::string &error)
    {
        const auto cells = dimension * dimension;
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto values_per_sec = elapsed_ms > 0.0 ? static_cast<double>(cells) * 1000.0 / elapsed_ms : 0.0;

        std::cout
            << "library=SEAL"
            << ",scheme=" << kSchemeName
            << ",operation=matrix_multiply_64x64_ctpt"
            << ",size=" << cells
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << elapsed_ms
            << ",ops_per_sec=" << ops_per_sec
            << ",values_per_sec=" << values_per_sec
            << ",matrix_dim=" << dimension
            << ",cells=" << cells
            << ",rotations_per_cell=" << rotation_steps(dimension).size()
            << ",plain_modulus=" << kPlainModulus
            << ",byte_size=" << byte_size;
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
        auto args = hebench::parse_benchmark_args(argc, argv);
        if (args.show_help)
        {
            std::cout << hebench::benchmark_usage(argv[0]);
            return 0;
        }
        if (args.corpus_path.find("matrix_a_") == std::string::npos)
        {
            args.corpus_path = "he_corpus/matrices/matrix_a_0064x0064.csv";
        }

        const auto matrix_a = hebench::read_dense_matrix_csv(args.corpus_path);
        const auto matrix_b = hebench::read_dense_matrix_csv(
            sibling_matrix_path(args.corpus_path, "matrix_b_0064x0064.csv"));
        if (matrix_a.rows != 64 || matrix_a.cols != 64 || matrix_b.rows != 64 || matrix_b.cols != 64)
        {
            throw std::runtime_error("matrix benchmark expects 64x64 matrix CSVs");
        }

        seal::EncryptionParameters parms(kSchemeType);
        parms.set_poly_modulus_degree(args.ring_size);
        parms.set_coeff_modulus(seal::CoeffModulus::BFVDefault(args.ring_size));
        parms.set_plain_modulus(kPlainModulus);

        seal::SEALContext context(parms);
        if (!context.parameters_set())
        {
            throw std::runtime_error(context.parameter_error_message());
        }

        seal::BatchEncoder encoder(context);
        const auto slot_count = encoder.slot_count();
        if (matrix_a.cols > slot_count / 2)
        {
            throw std::runtime_error("matrix dimension exceeds exact-scheme row slot count");
        }

        seal::KeyGenerator keygen(context);
        const auto secret_key = keygen.secret_key();
        seal::PublicKey public_key;
        seal::RelinKeys relin_keys;
        seal::GaloisKeys galois_keys;
        keygen.create_public_key(public_key);
        keygen.create_relin_keys(relin_keys);
        keygen.create_galois_keys(rotation_steps(64), galois_keys);

        seal::Encryptor encryptor(context, public_key);
        seal::Evaluator evaluator(context);
        seal::Decryptor decryptor(context, secret_key);

        std::vector<seal::Plaintext> b_columns(64);
        for (std::size_t col = 0; col < 64; ++col)
        {
            encoder.encode(matrix_col(matrix_b, col, slot_count), b_columns[col]);
        }

        bool correct = true;
        std::string error;
        std::size_t last_byte_size = 0;

        const hebench::Timer timer;
        for (std::size_t row = 0; row < 64; ++row)
        {
            seal::Plaintext row_plain;
            encoder.encode(matrix_row(matrix_a, row, slot_count), row_plain);
            seal::Ciphertext encrypted_row;
            encryptor.encrypt(row_plain, encrypted_row);

            for (std::size_t col = 0; col < 64; ++col)
            {
                seal::Ciphertext cell;
                evaluator.multiply_plain(encrypted_row, b_columns[col], cell);
                for (const auto step : rotation_steps(64))
                {
                    seal::Ciphertext rotated;
                    evaluator.rotate_rows(cell, step, galois_keys, rotated);
                    evaluator.add_inplace(cell, rotated);
                }

                seal::Plaintext cell_plain;
                std::vector<std::uint64_t> decoded;
                decryptor.decrypt(cell, cell_plain);
                encoder.decode(cell_plain, decoded);
                const auto actual = decoded.empty() ? 0 : decoded[0] % kPlainModulus;
                const auto expected_value = expected_exact_cell(matrix_a, matrix_b, row, col);
                if (actual != expected_value && correct)
                {
                    correct = false;
                    error = "cell (" + std::to_string(row) + "," + std::to_string(col) + ") expected " +
                        std::to_string(expected_value) + " got " + std::to_string(actual);
                }
                last_byte_size = serialized_size(cell);
            }
        }
        const auto elapsed_ms = timer.elapsed_ms();
        print_row(64, args.ring_size, correct, elapsed_ms, last_byte_size, error);
        return correct ? 0 : 2;
    }
    catch (const std::exception &error)
    {
        std::cerr << kBinaryName << " failed: " << error.what() << '\n';
        return 2;
    }
}
