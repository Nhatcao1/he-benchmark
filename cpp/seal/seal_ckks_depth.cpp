#include <algorithm>
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
#include "ckks_compare.hpp"
#include "ckks_config.hpp"
#include "csv_reader.hpp"
#include "depth_compare.hpp"
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
        std::size_t values_count,
        std::size_t byte_size,
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
        if (byte_size > 0)
        {
            std::cout << ",byte_size=" << byte_size;
        }
        if (!extra.empty())
        {
            std::cout << ',' << extra;
        }
        if (!g_ckks_config_extra.empty())
        {
            std::cout << ',' << g_ckks_config_extra;
        }
        std::cout << '\n';
    }

    std::vector<double> values_for(
        const std::vector<hebench::CkksDepthRow> &rows,
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

    std::size_t corpus_depth_count(const std::vector<hebench::CkksDepthRow> &rows)
    {
        const auto count = rows.front().expected_depth.size();
        for (const auto &row : rows)
        {
            if (row.expected_depth.size() != count)
            {
                throw std::runtime_error("inconsistent CKKS depth column count in corpus");
            }
        }
        return count;
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

        const auto rows = hebench::read_ckks_depth_csv(args.corpus_path);
        if (rows.empty())
        {
            throw std::runtime_error("corpus has no rows: " + args.corpus_path);
        }
        const auto max_depth = std::min(args.max_depth, corpus_depth_count(rows));
        auto ckks_config = hebench::ckks_config_for(args, max_depth, 30, 49);
        if (args.ckks_depth == 0)
        {
            // Depth benchmarks are explicitly driven by --max-depth. The shared
            // ring-sweep profile supplies small-ring security/limb settings,
            // but should not silently collapse a depth run to one level.
            ckks_config.multiplicative_depth = max_depth;
        }
        if (ckks_config.multiplicative_depth < max_depth)
        {
            throw std::runtime_error("--ckks-depth must be >= --max-depth for CKKS depth benchmarks");
        }
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
        seal::RelinKeys relin_keys;
        keygen.create_relin_keys(relin_keys);

        seal::Encryptor encryptor(context, public_key);
        seal::Evaluator evaluator(context);
        seal::Decryptor decryptor(context, secret_key);

        seal::Plaintext plain_a;
        seal::Plaintext plain_b;
        encoder.encode(values_for(rows, false, slot_count), hebench::ckks_scale(ckks_config), plain_a);
        encoder.encode(values_for(rows, true, slot_count), hebench::ckks_scale(ckks_config), plain_b);

        seal::Ciphertext encrypted_a;
        seal::Ciphertext encrypted_b;
        encryptor.encrypt(plain_a, encrypted_a);
        encryptor.encrypt(plain_b, encrypted_b);

        seal::Ciphertext current;
        bool keep_running = true;
        for (std::size_t depth = 1; depth <= max_depth && keep_running; ++depth)
        {
            const auto &left = depth == 1 ? encrypted_a : current;
            const auto &right = depth == 1 ? encrypted_b : current;
            const auto scale_before = left.scale();
            const auto level_before = left.coeff_modulus_size();

            seal::Ciphertext next;
            const hebench::Timer timer;
            evaluator.multiply(left, right, next);
            evaluator.relinearize_inplace(next, relin_keys);
            evaluator.rescale_to_next_inplace(next);
            const auto elapsed_ms = timer.elapsed_ms();

            const auto decoded = decrypt_decode(decryptor, encoder, next);
            const auto expected = hebench::ckks_depth_expected(rows, depth);
            const auto metrics = hebench::compare_ckks_vectors(decoded, expected);

            print_metric_row(
                "depth_mul",
                rows.size(),
                args.ring_size,
                metrics.correct,
                elapsed_ms,
                rows.size(),
                serialized_size(next),
                "depth=" + std::to_string(depth) +
                    ",max_depth=" + std::to_string(max_depth) +
                    ",scale_before=" + std::to_string(scale_before) +
                    ",scale_after=" + std::to_string(next.scale()) +
                    ",level_before=" + std::to_string(level_before) +
                    ",level_after=" + std::to_string(next.coeff_modulus_size()) +
                    ",components_after=" + std::to_string(next.size()) +
                    "," + metrics_extra(metrics));

            current = next;
            keep_running = metrics.correct;
        }

        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "seal_ckks_depth failed: " << error.what() << '\n';
        return 2;
    }
}
