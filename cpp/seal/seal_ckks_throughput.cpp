#include <algorithm>
#include <cmath>
#include <exception>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "seal/seal.h"

#include "benchmark_args.hpp"
#include "ckks_compare.hpp"
#include "ckks_config.hpp"
#include "csv_reader.hpp"
#include "throughput.hpp"

namespace
{
    constexpr const char *kLibraryName = "SEAL";
    constexpr const char *kSchemeName = "CKKS";

    std::string g_ckks_config_extra;

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

    std::vector<double> expected_values(
        const std::vector<hebench::CkksRow> &rows,
        hebench::CkksOperation operation)
    {
        std::vector<double> values;
        values.reserve(rows.size());
        for (const auto &row : rows)
        {
            values.push_back(hebench::expected_for(row, operation));
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

    std::string metrics_extra(const hebench::CkksMetrics &metrics)
    {
        std::ostringstream stream;
        stream
            << "pass_rate=" << metrics.pass_rate
            << ",passed=" << metrics.passed
            << ",mae=" << metrics.mae
            << ",rmse=" << metrics.rmse
            << ",max_abs_error=" << metrics.max_abs_error
            << ",mean_relative_error=" << metrics.mean_relative_error
            << ",max_relative_error=" << metrics.max_relative_error
            << ",precision_bits=" << metrics.precision_bits;
        return stream.str();
    }

    std::vector<double> decrypt_decode(
        seal::Decryptor &decryptor,
        seal::CKKSEncoder &encoder,
        const seal::Ciphertext &ciphertext)
    {
        seal::Plaintext plain;
        std::vector<double> decoded;
        decryptor.decrypt(ciphertext, plain);
        encoder.decode(plain, decoded);
        return decoded;
    }

    seal::Ciphertext multiply_plain_rescale(
        seal::Evaluator &evaluator,
        const seal::Ciphertext &encrypted,
        const seal::Plaintext &plain)
    {
        seal::Ciphertext result;
        evaluator.multiply_plain(encrypted, plain, result);
        evaluator.rescale_to_next_inplace(result);
        return result;
    }

    seal::Ciphertext dot_product_pt(
        seal::Evaluator &evaluator,
        const seal::Ciphertext &encrypted_a,
        const seal::Plaintext &plain_b,
        const seal::GaloisKeys &galois_keys,
        std::size_t active_slots)
    {
        auto result = multiply_plain_rescale(evaluator, encrypted_a, plain_b);
        for (std::size_t step = 1; step < active_slots; step *= 2)
        {
            seal::Ciphertext rotated;
            evaluator.rotate_vector(result, static_cast<int>(step), galois_keys, rotated);
            evaluator.add_inplace(result, rotated);
        }
        return result;
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
        if ((rows.size() & (rows.size() - 1)) != 0)
        {
            throw std::runtime_error("throughput dot product requires a power-of-two row count");
        }

        const auto ckks_config = hebench::ckks_config_for(args, 2, 40);
        g_ckks_config_extra = hebench::ckks_config_extra(ckks_config);

        seal::EncryptionParameters parms(seal::scheme_type::ckks);
        parms.set_poly_modulus_degree(args.ring_size);
        parms.set_coeff_modulus(seal::CoeffModulus::Create(
            args.ring_size, hebench::seal_ckks_coeff_modulus_bits(ckks_config)));

        seal::SEALContext context(parms);
        if (!context.parameters_set())
        {
            throw std::runtime_error(context.parameter_error_message());
        }

        seal::CKKSEncoder encoder(context);
        const auto slot_capacity = encoder.slot_count();
        if (rows.size() > slot_capacity)
        {
            throw std::runtime_error("corpus row count exceeds SEAL CKKS slot count");
        }

        seal::KeyGenerator keygen(context);
        const auto secret_key = keygen.secret_key();
        seal::PublicKey public_key;
        keygen.create_public_key(public_key);
        seal::GaloisKeys galois_keys;
        keygen.create_galois_keys(galois_keys);

        seal::Encryptor encryptor(context, public_key);
        seal::Evaluator evaluator(context);
        seal::Decryptor decryptor(context, secret_key);

        seal::Plaintext plain_a;
        seal::Plaintext plain_b;
        const auto scale = hebench::ckks_scale(ckks_config);
        encoder.encode(values_for(rows, false, slot_capacity), scale, plain_a);
        encoder.encode(values_for(rows, true, slot_capacity), scale, plain_b);

        seal::Ciphertext encrypted_a;
        encryptor.encrypt(plain_a, encrypted_a);

        bool all_correct = true;

        seal::Ciphertext sampled_encrypt;
        const auto encrypt_result = hebench::run_for_duration(args.duration_ms, [&] {
            encryptor.encrypt(plain_a, sampled_encrypt);
        });
        auto decoded = decrypt_decode(decryptor, encoder, sampled_encrypt);
        auto metrics = hebench::compare_ckks_vectors(decoded, values_for(rows, false, rows.size()));
        all_correct = metrics.correct && all_correct;
        hebench::print_throughput_row(
            kLibraryName,
            kSchemeName,
            "throughput_encrypt",
            rows.size(),
            args.ring_size,
            metrics.correct,
            hebench::throughput_base_extra(encrypt_result, args.duration_ms, rows.size(), slot_capacity) +
                "," + g_ckks_config_extra + "," + metrics_extra(metrics),
            metrics.error);

        seal::Ciphertext sampled_mul;
        const auto mul_result = hebench::run_for_duration(args.duration_ms, [&] {
            sampled_mul = multiply_plain_rescale(evaluator, encrypted_a, plain_b);
        });
        decoded = decrypt_decode(decryptor, encoder, sampled_mul);
        metrics = hebench::compare_ckks_vectors(decoded, expected_values(rows, hebench::CkksOperation::mul));
        all_correct = metrics.correct && all_correct;
        hebench::print_throughput_row(
            kLibraryName,
            kSchemeName,
            "throughput_mul_ct_pt",
            rows.size(),
            args.ring_size,
            metrics.correct,
            hebench::throughput_base_extra(mul_result, args.duration_ms, rows.size(), slot_capacity) +
                "," + g_ckks_config_extra + "," + metrics_extra(metrics),
            metrics.error);

        seal::Ciphertext sampled_dot;
        const auto dot_result = hebench::run_for_duration(args.duration_ms, [&] {
            sampled_dot = dot_product_pt(evaluator, encrypted_a, plain_b, galois_keys, rows.size());
        });
        decoded = decrypt_decode(decryptor, encoder, sampled_dot);
        const auto expected = expected_dot(rows);
        const auto actual = decoded.empty() ? 0.0 : decoded[0];
        metrics = hebench::compare_ckks_vectors(std::vector<double>{actual}, std::vector<double>{expected});
        all_correct = metrics.correct && all_correct;
        auto dot_extra = hebench::throughput_base_extra(dot_result, args.duration_ms, rows.size(), slot_capacity);
        dot_extra += "," + g_ckks_config_extra;
        dot_extra += ",completed_requests=" + std::to_string(dot_result.completed);
        dot_extra += ",requests_per_sec=" + std::to_string(hebench::safe_rate(dot_result.completed, dot_result.elapsed_seconds));
        dot_extra += ",input_values_per_sec=" + std::to_string(
            hebench::safe_rate(dot_result.completed, dot_result.elapsed_seconds) * static_cast<double>(rows.size()));
        dot_extra += ",expected=" + std::to_string(expected) + ",actual=" + std::to_string(actual);
        dot_extra += "," + metrics_extra(metrics);
        hebench::print_throughput_row(
            kLibraryName,
            kSchemeName,
            "throughput_dot_product_pt",
            rows.size(),
            args.ring_size,
            metrics.correct,
            dot_extra,
            metrics.error);

        std::string sampled_bytes;
        std::uint64_t total_bytes = 0;
        const auto serialize_result = hebench::run_for_duration(args.duration_ms, [&] {
            std::stringstream stream;
            encrypted_a.save(stream);
            sampled_bytes = stream.str();
            total_bytes += sampled_bytes.size();
        });
        seal::Ciphertext loaded;
        std::stringstream input(sampled_bytes);
        loaded.load(context, input);
        decoded = decrypt_decode(decryptor, encoder, loaded);
        metrics = hebench::compare_ckks_vectors(decoded, values_for(rows, false, rows.size()));
        all_correct = metrics.correct && all_correct;
        auto serialize_extra = hebench::throughput_base_extra(serialize_result, args.duration_ms, rows.size(), slot_capacity);
        serialize_extra += "," + g_ckks_config_extra;
        serialize_extra += ",total_serialized_bytes=" + std::to_string(total_bytes);
        serialize_extra += ",objects_per_sec=" + std::to_string(hebench::safe_rate(serialize_result.completed, serialize_result.elapsed_seconds));
        serialize_extra += ",mb_per_sec=" + std::to_string(
            serialize_result.elapsed_seconds > 0.0
                ? static_cast<double>(total_bytes) / serialize_result.elapsed_seconds / 1000000.0
                : 0.0);
        serialize_extra += ",sample_byte_size=" + std::to_string(sampled_bytes.size());
        serialize_extra += "," + metrics_extra(metrics);
        hebench::print_throughput_row(
            kLibraryName,
            kSchemeName,
            "throughput_serialize_ciphertext",
            rows.size(),
            args.ring_size,
            metrics.correct,
            serialize_extra,
            metrics.error);

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "seal_ckks_throughput failed: " << error.what() << '\n';
        return 2;
    }
}
