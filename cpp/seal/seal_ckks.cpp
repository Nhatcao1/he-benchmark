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
#include "ckks_compare.hpp"
#include "csv_reader.hpp"
#include "timer.hpp"

namespace
{
    using hebench::CkksOperation;

    std::string g_ckks_config_extra;

    template <typename T>
    std::size_t serialized_size(const T &value)
    {
        std::stringstream stream;
        value.save(stream);
        return stream.str().size();
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

    std::string metrics_extra(const hebench::CkksMetrics &metrics)
    {
        std::string extra =
            "pass_rate=" + std::to_string(metrics.pass_rate) +
            ",passed=" + std::to_string(metrics.passed) +
            ",mae=" + std::to_string(metrics.mae) +
            ",rmse=" + std::to_string(metrics.rmse) +
            ",max_abs_error=" + std::to_string(metrics.max_abs_error) +
            ",mean_relative_error=" + std::to_string(metrics.mean_relative_error) +
            ",max_relative_error=" + std::to_string(metrics.max_relative_error) +
            ",precision_bits=" + std::to_string(metrics.precision_bits);
        if (!metrics.correct)
        {
            extra += ",error=\"" + metrics.error + "\"";
        }
        return extra;
    }

    void print_metric_row(
        const std::string &operation,
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        double elapsed_ms,
        double ops_count,
        std::size_t values_count,
        std::size_t byte_size = 0,
        const std::string &extra = "")
    {
        const auto ops_per_sec = elapsed_ms > 0.0 ? ops_count * 1000.0 / elapsed_ms : 0.0;
        const auto values_per_sec = elapsed_ms > 0.0 ? static_cast<double>(values_count) * 1000.0 / elapsed_ms : 0.0;

        std::cout
            << "library=SEAL"
            << ",scheme=CKKS"
            << ",operation=" << operation
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
        if (byte_size > 0)
        {
            std::cout << ",byte_size=" << byte_size;
        }
        if (!extra.empty())
        {
            std::cout << ',' << extra;
        }
        std::cout << '\n';
    }

    bool compare_input(
        const std::vector<double> &actual,
        const std::vector<hebench::CkksRow> &rows,
        bool use_b,
        std::string &error)
    {
        if (actual.size() < rows.size())
        {
            error = "decoded slot count is smaller than corpus row count";
            return false;
        }

        double max_abs = 0.0;
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto expected = use_b ? rows[i].b : rows[i].a;
            max_abs = std::max(max_abs, std::abs(actual[i] - expected));
            if (std::abs(actual[i] - expected) > 1e-3)
            {
                error = "slot " + std::to_string(i) + " expected " + std::to_string(expected) +
                    " got " + std::to_string(actual[i]);
                return false;
            }
        }

        error = "max_abs_error=" + std::to_string(max_abs);
        return true;
    }

    bool run_operation(
        seal::Evaluator &evaluator,
        seal::Decryptor &decryptor,
        seal::CKKSEncoder &encoder,
        const std::vector<hebench::CkksRow> &rows,
        const seal::Ciphertext &encrypted_a,
        const seal::Ciphertext &encrypted_b,
        const seal::Plaintext &plain_b,
        const seal::RelinKeys &relin_keys,
        CkksOperation operation,
        std::size_t ring_size)
    {
        seal::Ciphertext result;
        const auto scale_before = encrypted_a.scale();
        const auto level_before = encrypted_a.coeff_modulus_size();
        const hebench::Timer timer;

        switch (operation)
        {
        case CkksOperation::add:
            evaluator.add(encrypted_a, encrypted_b, result);
            break;
        case CkksOperation::sub:
            evaluator.sub(encrypted_a, encrypted_b, result);
            break;
        case CkksOperation::mul:
        case CkksOperation::rescale:
            evaluator.multiply(encrypted_a, encrypted_b, result);
            evaluator.relinearize_inplace(result, relin_keys);
            evaluator.rescale_to_next_inplace(result);
            break;
        case CkksOperation::add_plain:
            evaluator.add_plain(encrypted_a, plain_b, result);
            break;
        case CkksOperation::sub_plain:
            evaluator.sub_plain(encrypted_a, plain_b, result);
            break;
        case CkksOperation::mul_plain:
            evaluator.multiply_plain(encrypted_a, plain_b, result);
            evaluator.rescale_to_next_inplace(result);
            break;
        case CkksOperation::square_a:
            evaluator.square(encrypted_a, result);
            evaluator.relinearize_inplace(result, relin_keys);
            evaluator.rescale_to_next_inplace(result);
            break;
        case CkksOperation::negate_a:
            evaluator.negate(encrypted_a, result);
            break;
        }

        const auto elapsed_ms = timer.elapsed_ms();
        const auto scale_after = result.scale();
        const auto level_after = result.coeff_modulus_size();
        const auto decoded = decrypt_decode(decryptor, encoder, result);
        const auto metrics = hebench::compare_ckks_slots(decoded, rows, operation);

        print_metric_row(
            hebench::operation_name(operation),
            rows.size(),
            ring_size,
            metrics.correct,
            elapsed_ms,
            1.0,
            rows.size(),
            serialized_size(result),
            "scale_before=" + std::to_string(scale_before) +
                ",scale_after=" + std::to_string(scale_after) +
                ",level_before=" + std::to_string(level_before) +
                ",level_after=" + std::to_string(level_after) +
                "," + metrics_extra(metrics));
        return metrics.correct;
    }

    bool run_relinearization(
        seal::Evaluator &evaluator,
        seal::Decryptor &decryptor,
        seal::CKKSEncoder &encoder,
        const std::vector<hebench::CkksRow> &rows,
        const seal::Ciphertext &encrypted_a,
        const seal::Ciphertext &encrypted_b,
        const seal::RelinKeys &relin_keys,
        std::size_t ring_size)
    {
        seal::Ciphertext product;
        evaluator.multiply(encrypted_a, encrypted_b, product);
        const auto size_before = serialized_size(product);
        const auto components_before = product.size();

        seal::Ciphertext result = product;
        const hebench::Timer timer;
        evaluator.relinearize_inplace(result, relin_keys);
        const auto elapsed_ms = timer.elapsed_ms();

        const auto decoded = decrypt_decode(decryptor, encoder, result);
        const auto metrics = hebench::compare_ckks_slots(decoded, rows, CkksOperation::mul);
        const auto size_after = serialized_size(result);
        const auto ratio = size_before > 0 ? static_cast<double>(size_after) / static_cast<double>(size_before) : 0.0;

        print_metric_row(
            "relin",
            rows.size(),
            ring_size,
            metrics.correct,
            elapsed_ms,
            1.0,
            rows.size(),
            size_after,
            "size_before_bytes=" + std::to_string(size_before) +
                ",size_after_bytes=" + std::to_string(size_after) +
                ",components_before=" + std::to_string(components_before) +
                ",components_after=" + std::to_string(result.size()) +
                ",reduction_ratio=" + std::to_string(ratio) +
                "," + metrics_extra(metrics));
        return metrics.correct;
    }

    std::vector<double> expected_rotation(
        const std::vector<hebench::CkksRow> &rows,
        std::size_t slot_count,
        int step,
        bool left)
    {
        const auto base = values_for(rows, false, slot_count);
        std::vector<double> expected(rows.size(), 0.0);
        const auto normalized_step = static_cast<std::size_t>(
            ((step % static_cast<int>(slot_count)) + static_cast<int>(slot_count)) %
            static_cast<int>(slot_count));

        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto source = left
                ? (i + normalized_step) % slot_count
                : (i + slot_count - normalized_step) % slot_count;
            expected[i] = base[source];
        }
        return expected;
    }

    bool run_rotation(
        seal::Evaluator &evaluator,
        seal::Decryptor &decryptor,
        seal::CKKSEncoder &encoder,
        const std::vector<hebench::CkksRow> &rows,
        const seal::Ciphertext &encrypted_a,
        const seal::GaloisKeys &galois_keys,
        int step,
        std::size_t ring_size,
        std::size_t slot_count)
    {
        seal::Ciphertext result;
        const hebench::Timer timer;
        evaluator.rotate_vector(encrypted_a, step, galois_keys, result);
        const auto elapsed_ms = timer.elapsed_ms();

        const auto decoded = decrypt_decode(decryptor, encoder, result);
        auto metrics = hebench::compare_ckks_vectors(decoded, expected_rotation(rows, slot_count, step, true));
        std::string direction = "left";
        if (!metrics.correct)
        {
            const auto right_metrics = hebench::compare_ckks_vectors(decoded, expected_rotation(rows, slot_count, step, false));
            if (right_metrics.correct || right_metrics.pass_rate > metrics.pass_rate)
            {
                metrics = right_metrics;
                direction = "right";
            }
        }

        print_metric_row(
            "rotate_" + std::to_string(step),
            rows.size(),
            ring_size,
            metrics.correct,
            elapsed_ms,
            1.0,
            rows.size(),
            serialized_size(result),
            "rotation_step=" + std::to_string(step) +
                ",matched_direction=" + direction +
                "," + metrics_extra(metrics));
        return metrics.correct;
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

        const hebench::Timer keygen_timer;
        seal::KeyGenerator keygen(context);
        const auto secret_key = keygen.secret_key();
        seal::PublicKey public_key;
        keygen.create_public_key(public_key);
        const auto keygen_ms = keygen_timer.elapsed_ms();

        const hebench::Timer relin_keygen_timer;
        seal::RelinKeys relin_keys;
        keygen.create_relin_keys(relin_keys);
        const auto relin_keygen_ms = relin_keygen_timer.elapsed_ms();

        const hebench::Timer rotation_keygen_timer;
        seal::GaloisKeys galois_keys;
        keygen.create_galois_keys(std::vector<int>{1, -1, 8}, galois_keys);
        const auto rotation_keygen_ms = rotation_keygen_timer.elapsed_ms();

        print_metric_row("keygen", rows.size(), args.ring_size, true, keygen_ms, 1.0, 0,
            serialized_size(secret_key) + serialized_size(public_key),
            "secret_key_bytes=" + std::to_string(serialized_size(secret_key)) +
                ",public_key_bytes=" + std::to_string(serialized_size(public_key)));
        print_metric_row("relin_keygen", rows.size(), args.ring_size, true, relin_keygen_ms, 1.0, 0,
            serialized_size(relin_keys));
        print_metric_row("rotation_keygen", rows.size(), args.ring_size, true, rotation_keygen_ms, 1.0, 0,
            serialized_size(galois_keys));

        seal::Encryptor encryptor(context, public_key);
        seal::Evaluator evaluator(context);
        seal::Decryptor decryptor(context, secret_key);

        seal::Plaintext plain_a;
        seal::Plaintext plain_b;
        const hebench::Timer encode_timer;
        encoder.encode(values_for(rows, false, slot_count), hebench::ckks_scale(ckks_config), plain_a);
        encoder.encode(values_for(rows, true, slot_count), hebench::ckks_scale(ckks_config), plain_b);
        const auto encode_ms = encode_timer.elapsed_ms();
        print_metric_row("encode", rows.size(), args.ring_size, true, encode_ms, 2.0, rows.size() * 2,
            serialized_size(plain_a) + serialized_size(plain_b));

        const hebench::Timer decode_timer;
        std::vector<double> decoded_a;
        std::vector<double> decoded_b;
        encoder.decode(plain_a, decoded_a);
        encoder.decode(plain_b, decoded_b);
        const auto decode_ms = decode_timer.elapsed_ms();
        std::string input_error;
        const auto decode_correct =
            compare_input(decoded_a, rows, false, input_error) &&
            compare_input(decoded_b, rows, true, input_error);
        print_metric_row("decode", rows.size(), args.ring_size, decode_correct, decode_ms, 2.0, rows.size() * 2,
            0, decode_correct ? "" : "error=\"" + input_error + "\"");

        seal::Ciphertext encrypted_a;
        seal::Ciphertext encrypted_b;
        const hebench::Timer encrypt_timer;
        encryptor.encrypt(plain_a, encrypted_a);
        encryptor.encrypt(plain_b, encrypted_b);
        const auto encrypt_ms = encrypt_timer.elapsed_ms();
        print_metric_row("encrypt", rows.size(), args.ring_size, true, encrypt_ms, 2.0, rows.size() * 2,
            serialized_size(encrypted_a) + serialized_size(encrypted_b));

        const hebench::Timer decrypt_timer;
        const auto decrypted_a = decrypt_decode(decryptor, encoder, encrypted_a);
        const auto decrypted_b = decrypt_decode(decryptor, encoder, encrypted_b);
        const auto decrypt_ms = decrypt_timer.elapsed_ms();
        const auto decrypt_correct =
            compare_input(decrypted_a, rows, false, input_error) &&
            compare_input(decrypted_b, rows, true, input_error);
        print_metric_row("decrypt", rows.size(), args.ring_size, decrypt_correct, decrypt_ms, 2.0, rows.size() * 2,
            0, decrypt_correct ? "" : "error=\"" + input_error + "\"");

        bool all_correct = decode_correct && decrypt_correct;
        all_correct = run_relinearization(
            evaluator, decryptor, encoder, rows, encrypted_a, encrypted_b, relin_keys, args.ring_size) && all_correct;

        for (const auto step : {1, -1, 8})
        {
            all_correct = run_rotation(
                evaluator,
                decryptor,
                encoder,
                rows,
                encrypted_a,
                galois_keys,
                step,
                args.ring_size,
                slot_count) && all_correct;
        }

        for (const auto operation : {
                 CkksOperation::add,
                 CkksOperation::sub,
                 CkksOperation::mul,
                 CkksOperation::add_plain,
                 CkksOperation::sub_plain,
                 CkksOperation::mul_plain,
                 CkksOperation::square_a,
                 CkksOperation::negate_a,
                 CkksOperation::rescale,
             })
        {
            all_correct = run_operation(
                evaluator,
                decryptor,
                encoder,
                rows,
                encrypted_a,
                encrypted_b,
                plain_b,
                relin_keys,
                operation,
                args.ring_size) && all_correct;
        }

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "seal_ckks failed: " << error.what() << '\n';
        return 2;
    }
}
