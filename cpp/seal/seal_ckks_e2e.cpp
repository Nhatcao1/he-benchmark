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
#include "ckks_config.hpp"
#include "csv_reader.hpp"
#include "memory_usage.hpp"
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

    double expected_sum(const std::vector<hebench::CkksRow> &rows)
    {
        double sum = 0.0;
        for (const auto &row : rows)
        {
            sum += row.a;
        }
        return sum;
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

    template <typename T>
    std::string save_to_string(const T &value)
    {
        std::stringstream stream;
        value.save(stream);
        return stream.str();
    }

    template <typename T>
    void load_from_string(const seal::SEALContext &context, const std::string &bytes, T &value)
    {
        std::stringstream stream(bytes);
        value.load(context, stream);
    }

    seal::Ciphertext rotate_sum(
        seal::Evaluator &evaluator,
        const seal::GaloisKeys &galois_keys,
        const seal::Ciphertext &input,
        std::size_t size)
    {
        seal::Ciphertext result = input;
        for (const auto step : rotation_steps(size))
        {
            seal::Ciphertext rotated;
            evaluator.rotate_vector(result, step, galois_keys, rotated);
            evaluator.add_inplace(result, rotated);
        }
        return result;
    }

    void print_row(
        const std::string &operation,
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        double total_ms,
        double server_eval_ms,
        std::size_t request_bytes,
        std::size_t response_bytes,
        std::size_t rotations_count,
        std::size_t components_after,
        double scale_after,
        std::size_t level_after,
        double expected,
        double actual)
    {
        const auto requests_per_sec = total_ms > 0.0 ? 1000.0 / total_ms : 0.0;
        const auto values_per_sec = total_ms > 0.0 ? static_cast<double>(size) * 1000.0 / total_ms : 0.0;
        const auto abs_error = std::abs(actual - expected);
        const auto relative_error = abs_error / std::max(std::abs(expected), 1e-12);
        const auto precision_bits = relative_error > 0.0 ? -std::log2(relative_error) : INFINITY;

        std::cout
            << "library=SEAL"
            << ",scheme=CKKS"
            << ",operation=" << operation
            << ",size=" << size
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << total_ms
            << ",server_eval_latency_ms=" << server_eval_ms
            << ",ops_per_sec=" << requests_per_sec
            << ",requests_per_sec=" << requests_per_sec
            << ",values_per_sec=" << values_per_sec;
        if (!g_ckks_config_extra.empty())
        {
            std::cout << ',' << g_ckks_config_extra;
        }
        std::cout
            << ",request_bytes=" << request_bytes
            << ",response_bytes=" << response_bytes
            << ",peak_rss_kb=" << hebench::peak_rss_kb()
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
            throw std::runtime_error("end-to-end workloads require a power-of-two row count");
        }

        const auto ckks_config = hebench::ckks_config_for(args, 2, 40);
        g_ckks_config_extra = hebench::ckks_config_extra(ckks_config);

        seal::EncryptionParameters parms(seal::scheme_type::ckks);
        parms.set_poly_modulus_degree(args.ring_size);
        parms.set_coeff_modulus(seal::CoeffModulus::Create(
            args.ring_size, hebench::seal_ckks_coeff_modulus_bits(ckks_config)));

        seal::SEALContext context(
            parms,
            true,
            ckks_config.relaxed_security ? seal::sec_level_type::none : seal::sec_level_type::tc128);
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

        seal::Plaintext plain_a;
        seal::Plaintext plain_b;
        encoder.encode(values_for(rows, false, slot_count), hebench::ckks_scale(ckks_config), plain_a);
        encoder.encode(values_for(rows, true, slot_count), hebench::ckks_scale(ckks_config), plain_b);

        bool all_correct = true;
        for (const auto operation : {"end_to_end_sum", "end_to_end_dot_product_pt", "end_to_end_dot_product_ct"})
        {
            const std::string operation_name = operation;
            const bool is_sum = operation_name == "end_to_end_sum";
            const bool is_dot_ct = operation_name == "end_to_end_dot_product_ct";
            const auto expected = is_sum ? expected_sum(rows) : expected_dot(rows);

            const hebench::Timer total_timer;
            seal::Ciphertext encrypted_request;
            encryptor.encrypt(plain_a, encrypted_request);
            const auto request_bytes = save_to_string(encrypted_request);
            std::string request_b_bytes;
            if (is_dot_ct)
            {
                seal::Ciphertext encrypted_request_b;
                encryptor.encrypt(plain_b, encrypted_request_b);
                request_b_bytes = save_to_string(encrypted_request_b);
            }

            seal::Ciphertext server_request;
            load_from_string(context, request_bytes, server_request);
            seal::Ciphertext server_request_b;
            if (is_dot_ct)
            {
                load_from_string(context, request_b_bytes, server_request_b);
            }

            const hebench::Timer server_timer;
            seal::Ciphertext server_result;
            if (is_sum)
            {
                server_result = rotate_sum(evaluator, galois_keys, server_request, rows.size());
            }
            else if (is_dot_ct)
            {
                evaluator.multiply(server_request, server_request_b, server_result);
                evaluator.relinearize_inplace(server_result, relin_keys);
                evaluator.rescale_to_next_inplace(server_result);
                server_result = rotate_sum(evaluator, galois_keys, server_result, rows.size());
            }
            else
            {
                evaluator.multiply_plain(server_request, plain_b, server_result);
                evaluator.rescale_to_next_inplace(server_result);
                server_result = rotate_sum(evaluator, galois_keys, server_result, rows.size());
            }
            const auto server_eval_ms = server_timer.elapsed_ms();
            const auto response_bytes = save_to_string(server_result);

            seal::Ciphertext client_response;
            load_from_string(context, response_bytes, client_response);
            seal::Plaintext result_plain;
            std::vector<double> decoded;
            decryptor.decrypt(client_response, result_plain);
            encoder.decode(result_plain, decoded);
            const auto total_ms = total_timer.elapsed_ms();

            const auto actual = decoded.empty() ? 0.0 : decoded[0];
            const auto abs_error = std::abs(actual - expected);
            const auto relative_error = abs_error / std::max(std::abs(expected), 1e-12);
            const bool correct = abs_error <= 1e-3 || relative_error <= 1e-3;
            all_correct = correct && all_correct;

            print_row(
                operation,
                rows.size(),
                args.ring_size,
                correct,
                total_ms,
                server_eval_ms,
                request_bytes.size() + request_b_bytes.size(),
                response_bytes.size(),
                rotation_steps(rows.size()).size(),
                client_response.size(),
                client_response.scale(),
                client_response.coeff_modulus_size(),
                expected,
                actual);
        }

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "seal_ckks_e2e failed: " << error.what() << '\n';
        return 2;
    }
}
