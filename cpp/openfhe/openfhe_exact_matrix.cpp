#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "openfhe.h"

#include "benchmark_args.hpp"
#include "csv_reader.hpp"
#include "timer.hpp"

namespace
{
#ifdef HE_BENCHMARK_OPENFHE_MATRIX_BGV
    constexpr const char *kSchemeName = "BGV";
    constexpr const char *kBinaryName = "openfhe_bgv_matrix";
#else
    constexpr const char *kSchemeName = "BFV";
    constexpr const char *kBinaryName = "openfhe_bfv_matrix";
#endif

    constexpr std::uint64_t kPlainModulus = 786433;

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

    std::vector<std::int64_t> matrix_row(
        const hebench::DenseMatrix &matrix,
        std::size_t row,
        std::size_t slot_count)
    {
        std::vector<std::int64_t> values(slot_count, 0);
        for (std::size_t col = 0; col < matrix.cols; ++col)
        {
            values[col] = static_cast<std::int64_t>(normalize_exact(matrix.at(row, col)));
        }
        return values;
    }

    std::vector<std::int64_t> matrix_col(
        const hebench::DenseMatrix &matrix,
        std::size_t col,
        std::size_t slot_count)
    {
        std::vector<std::int64_t> values(slot_count, 0);
        for (std::size_t row = 0; row < matrix.rows; ++row)
        {
            values[row] = static_cast<std::int64_t>(normalize_exact(matrix.at(row, col)));
        }
        return values;
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
        const std::string &error)
    {
        const auto cells = dimension * dimension;
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto values_per_sec = elapsed_ms > 0.0 ? static_cast<double>(cells) * 1000.0 / elapsed_ms : 0.0;

        std::cout
            << "library=OpenFHE"
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
            << ",plain_modulus=" << kPlainModulus;
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

#ifdef HE_BENCHMARK_OPENFHE_MATRIX_BGV
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
        crypto_context->Enable(lbcrypto::ADVANCEDSHE);

        auto keys = crypto_context->KeyGen();
        crypto_context->EvalMultKeyGen(keys.secretKey);
        crypto_context->EvalRotateKeyGen(keys.secretKey, rotation_steps(64));

        const auto slot_count = args.ring_size;
        std::vector<lbcrypto::Plaintext> b_columns(64);
        for (std::size_t col = 0; col < 64; ++col)
        {
            b_columns[col] = crypto_context->MakePackedPlaintext(matrix_col(matrix_b, col, slot_count));
        }

        bool correct = true;
        std::string error;

        const hebench::Timer timer;
        for (std::size_t row = 0; row < 64; ++row)
        {
            auto row_plain = crypto_context->MakePackedPlaintext(matrix_row(matrix_a, row, slot_count));
            auto encrypted_row = crypto_context->Encrypt(keys.publicKey, row_plain);

            for (std::size_t col = 0; col < 64; ++col)
            {
                auto cell = crypto_context->EvalMult(encrypted_row, b_columns[col]);
                for (const auto step : rotation_steps(64))
                {
                    auto rotated = crypto_context->EvalRotate(cell, step);
                    cell = crypto_context->EvalAdd(cell, rotated);
                }

                lbcrypto::Plaintext cell_plain;
                crypto_context->Decrypt(keys.secretKey, cell, &cell_plain);
                cell_plain->SetLength(1);
                const auto decoded = cell_plain->GetPackedValue();
                const auto actual = decoded.empty() ? 0 : normalize_exact(static_cast<double>(decoded[0]));
                const auto expected_value = expected_exact_cell(matrix_a, matrix_b, row, col);
                if (actual != expected_value && correct)
                {
                    correct = false;
                    error = "cell (" + std::to_string(row) + "," + std::to_string(col) + ") expected " +
                        std::to_string(expected_value) + " got " + std::to_string(actual);
                }
            }
        }
        const auto elapsed_ms = timer.elapsed_ms();
        print_row(64, args.ring_size, correct, elapsed_ms, error);
        return correct ? 0 : 2;
    }
    catch (const std::exception &error)
    {
        std::cerr << kBinaryName << " failed: " << error.what() << '\n';
        return 2;
    }
}
