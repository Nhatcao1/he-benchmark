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

    std::string sibling_matrix_path(const std::string &matrix_a_path, const std::string &filename)
    {
        const auto slash = matrix_a_path.find_last_of("/\\");
        if (slash == std::string::npos)
        {
            return filename;
        }
        return matrix_a_path.substr(0, slash + 1) + filename;
    }

    std::vector<double> matrix_row(const hebench::DenseMatrix &matrix, std::size_t row, std::size_t slot_count)
    {
        std::vector<double> values(slot_count, 0.0);
        for (std::size_t col = 0; col < matrix.cols; ++col)
        {
            values[col] = matrix.at(row, col);
        }
        return values;
    }

    std::vector<double> matrix_col(const hebench::DenseMatrix &matrix, std::size_t col, std::size_t slot_count)
    {
        std::vector<double> values(slot_count, 0.0);
        for (std::size_t row = 0; row < matrix.rows; ++row)
        {
            values[row] = matrix.at(row, col);
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

    void print_row(
        std::size_t dimension,
        std::size_t ring_size,
        bool correct,
        double elapsed_ms,
        double max_abs_error,
        double rmse)
    {
        const auto cells = dimension * dimension;
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto values_per_sec = elapsed_ms > 0.0 ? static_cast<double>(cells) * 1000.0 / elapsed_ms : 0.0;
        std::cout
            << "library=OpenFHE"
            << ",scheme=CKKS"
            << ",operation=matrix_multiply_64x64_ctpt"
            << ",size=" << cells
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
            << ",matrix_dim=" << dimension
            << ",cells=" << cells
            << ",rotations_per_cell=" << rotation_steps(dimension).size()
            << ",max_abs_error=" << max_abs_error
            << ",rmse=" << rmse
            << '\n';
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
        const auto expected = hebench::read_dense_matrix_csv(
            sibling_matrix_path(args.corpus_path, "matrix_expected_0064x0064.csv"));
        if (matrix_a.rows != 64 || matrix_a.cols != 64 || matrix_b.rows != 64 || matrix_b.cols != 64 ||
            expected.rows != 64 || expected.cols != 64)
        {
            throw std::runtime_error("matrix benchmark expects 64x64 matrix CSVs");
        }

        lbcrypto::CCParams<lbcrypto::CryptoContextCKKSRNS> parameters;
        const auto ckks_config = hebench::ckks_config_for(args, 2, 40, 60);
        g_ckks_config_extra = hebench::ckks_config_extra(ckks_config);
        parameters.SetMultiplicativeDepth(static_cast<std::uint32_t>(ckks_config.multiplicative_depth));
        parameters.SetScalingModSize(static_cast<std::uint32_t>(ckks_config.scale_bits));
        parameters.SetFirstModSize(static_cast<std::uint32_t>(ckks_config.first_mod_bits));
        if (ckks_config.relaxed_security)
        {
            parameters.SetSecurityLevel(lbcrypto::HEStd_NotSet);
        }
        else
        {
            parameters.SetSecurityLevel(lbcrypto::HEStd_128_classic);
        }
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size / 2));
        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);
        crypto_context->Enable(lbcrypto::ADVANCEDSHE);

        auto keys = crypto_context->KeyGen();
        crypto_context->EvalRotateKeyGen(keys.secretKey, rotation_steps(64));

        const auto slot_count = args.ring_size / 2;
        std::vector<lbcrypto::Plaintext> b_columns(64);
        for (std::size_t col = 0; col < 64; ++col)
        {
            b_columns[col] = crypto_context->MakeCKKSPackedPlaintext(matrix_col(matrix_b, col, slot_count));
        }

        double sum_square_error = 0.0;
        double max_abs_error = 0.0;

        const hebench::Timer timer;
        for (std::size_t row = 0; row < 64; ++row)
        {
            auto row_plain = crypto_context->MakeCKKSPackedPlaintext(matrix_row(matrix_a, row, slot_count));
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
                const auto decoded = cell_plain->GetRealPackedValue();
                const auto actual = decoded.empty() ? 0.0 : decoded[0];
                const auto error = std::abs(actual - expected.at(row, col));
                sum_square_error += error * error;
                max_abs_error = std::max(max_abs_error, error);
            }
        }
        const auto elapsed_ms = timer.elapsed_ms();
        const auto rmse = std::sqrt(sum_square_error / 4096.0);
        print_row(64, args.ring_size, max_abs_error <= 1e-2 || rmse <= 1e-3, elapsed_ms, max_abs_error, rmse);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "openfhe_ckks_matrix failed: " << error.what() << '\n';
        return 2;
    }
}
