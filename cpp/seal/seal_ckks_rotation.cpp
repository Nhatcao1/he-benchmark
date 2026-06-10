#include <cmath>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "seal/seal.h"

#include "benchmark_args.hpp"
#include "ckks_compare.hpp"
#include "ckks_config.hpp"
#include "csv_reader.hpp"
#include "timer.hpp"

namespace
{
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
        std::size_t slot_count)
    {
        std::vector<double> values(slot_count, 0.0);
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            values[i] = rows[i].a;
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

    void print_row(
        const std::string &operation,
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        double elapsed_ms,
        std::size_t values_count,
        std::size_t byte_size = 0,
        const std::string &extra = "")
    {
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
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

    std::vector<double> expected_rotation(
        const std::vector<hebench::CkksRow> &rows,
        std::size_t slot_count,
        int step,
        bool left)
    {
        const auto base = values_for(rows, slot_count);
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

        print_row(
            "rotate_" + std::to_string(step),
            rows.size(),
            ring_size,
            metrics.correct,
            elapsed_ms,
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

        seal::KeyGenerator keygen(context);
        const auto secret_key = keygen.secret_key();
        seal::PublicKey public_key;
        keygen.create_public_key(public_key);

        const hebench::Timer rotation_keygen_timer;
        seal::GaloisKeys galois_keys;
        keygen.create_galois_keys(std::vector<int>{1, -1, 8}, galois_keys);
        const auto rotation_keygen_ms = rotation_keygen_timer.elapsed_ms();
        print_row("rotation_keygen", rows.size(), args.ring_size, true, rotation_keygen_ms, 0, serialized_size(galois_keys));

        seal::Encryptor encryptor(context, public_key);
        seal::Evaluator evaluator(context);
        seal::Decryptor decryptor(context, secret_key);

        seal::Plaintext plain_a;
        encoder.encode(values_for(rows, slot_count), hebench::ckks_scale(ckks_config), plain_a);
        seal::Ciphertext encrypted_a;
        encryptor.encrypt(plain_a, encrypted_a);

        bool all_correct = true;
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

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "seal_ckks_rotation failed: " << error.what() << '\n';
        return 2;
    }
}
