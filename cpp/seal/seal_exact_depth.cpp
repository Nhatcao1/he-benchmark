#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "seal/seal.h"

#include "benchmark_args.hpp"
#include "csv_reader.hpp"
#include "depth_compare.hpp"
#include "timer.hpp"

namespace
{
    constexpr std::uint64_t kPlainModulusBitSize = 20;

#ifdef HE_BENCHMARK_SEAL_DEPTH_BGV
    constexpr auto kScheme = seal::scheme_type::bgv;
    constexpr const char *kSchemeName = "BGV";
    constexpr const char *kBinaryName = "seal_bgv_depth";
#else
    constexpr auto kScheme = seal::scheme_type::bfv;
    constexpr const char *kSchemeName = "BFV";
    constexpr const char *kBinaryName = "seal_bfv_depth";
#endif

    template <typename T>
    std::size_t serialized_size(const T &value)
    {
        std::stringstream stream;
        value.save(stream);
        return stream.str().size();
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
            << ",scheme=" << kSchemeName
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
        std::cout << '\n';
    }

    std::vector<std::uint64_t> encode_signed_inputs(
        const std::vector<hebench::ExactDepthRow> &rows,
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

    std::vector<std::uint64_t> decrypt_decode(
        seal::Decryptor &decryptor,
        seal::BatchEncoder &encoder,
        const seal::Ciphertext &ciphertext)
    {
        seal::Plaintext plain;
        std::vector<std::uint64_t> decoded;
        decryptor.decrypt(ciphertext, plain);
        encoder.decode(plain, decoded);
        return decoded;
    }

    std::size_t corpus_depth_count(const std::vector<hebench::ExactDepthRow> &rows)
    {
        const auto count = rows.front().expected_depth.size();
        for (const auto &row : rows)
        {
            if (row.expected_depth.size() != count)
            {
                throw std::runtime_error("inconsistent exact depth column count in corpus");
            }
        }
        return count;
    }

    std::size_t chain_index_for(
        const seal::SEALContext &context,
        const seal::Ciphertext &ciphertext)
    {
        const auto data = context.get_context_data(ciphertext.parms_id());
        return data == nullptr ? 0 : data->chain_index();
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

        const auto rows = hebench::read_exact_depth_csv(args.corpus_path);
        if (rows.empty())
        {
            throw std::runtime_error("corpus has no rows: " + args.corpus_path);
        }
        const auto max_depth = std::min(args.max_depth, corpus_depth_count(rows));

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
        const auto plain_modulus = parms.plain_modulus().value();
        if (rows.size() > slot_count)
        {
            throw std::runtime_error("corpus row count exceeds SEAL slot count");
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
        encoder.encode(encode_signed_inputs(rows, false, slot_count, plain_modulus), plain_a);
        encoder.encode(encode_signed_inputs(rows, true, slot_count, plain_modulus), plain_b);

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
            const auto level_before = chain_index_for(context, left);
            const auto noise_before = decryptor.invariant_noise_budget(left);

            seal::Ciphertext next;
            const hebench::Timer timer;
            evaluator.multiply(left, right, next);
            evaluator.relinearize_inplace(next, relin_keys);
            const auto elapsed_ms = timer.elapsed_ms();

            const auto level_after = chain_index_for(context, next);
            const auto noise_after = decryptor.invariant_noise_budget(next);
            const auto decoded = decrypt_decode(decryptor, encoder, next);
            const auto expected = hebench::exact_depth_expected(rows, depth, plain_modulus);

            std::string error;
            const bool correct = hebench::compare_exact_depth_slots(decoded, expected, plain_modulus, error);
            print_metric_row(
                "depth_mul",
                rows.size(),
                args.ring_size,
                correct,
                elapsed_ms,
                rows.size(),
                serialized_size(next),
                "depth=" + std::to_string(depth) +
                    ",max_depth=" + std::to_string(max_depth) +
                    ",level_before=" + std::to_string(level_before) +
                    ",level_after=" + std::to_string(level_after) +
                    ",noise_budget_before_bits=" + std::to_string(noise_before) +
                    ",noise_budget_after_bits=" + std::to_string(noise_after) +
                    ",components_after=" + std::to_string(next.size()) +
                    (correct ? "" : ",error=\"" + error + "\""));

            current = next;
            keep_running = correct;
        }

        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << kBinaryName << " failed: " << error.what() << '\n';
        return 2;
    }
}
