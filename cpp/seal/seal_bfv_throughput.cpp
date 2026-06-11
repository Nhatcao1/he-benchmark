#include <cstdint>
#include <exception>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "seal/seal.h"

#include "benchmark_args.hpp"
#include "csv_reader.hpp"
#include "exact_compare.hpp"
#include "throughput.hpp"

namespace
{
    constexpr std::uint64_t kPlainModulusBitSize = 20;
    constexpr const char *kLibraryName = "SEAL";
#ifdef HE_BENCHMARK_SEAL_THROUGHPUT_BGV
    constexpr const char *kSchemeName = "BGV";
    constexpr const char *kBinaryName = "seal_bgv_throughput";
    constexpr seal::scheme_type kSchemeType = seal::scheme_type::bgv;
#else
    constexpr const char *kSchemeName = "BFV";
    constexpr const char *kBinaryName = "seal_bfv_throughput";
    constexpr seal::scheme_type kSchemeType = seal::scheme_type::bfv;
#endif

    std::int64_t centered_mod_wide(__int128 value, std::uint64_t plain_modulus)
    {
        const auto modulus = static_cast<__int128>(plain_modulus);
        auto reduced = value % modulus;
        if (reduced < 0)
        {
            reduced += modulus;
        }
        if (reduced > modulus / 2)
        {
            reduced -= modulus;
        }
        return static_cast<std::int64_t>(reduced);
    }

    std::int64_t expected_dot(const std::vector<hebench::ExactRow> &rows, std::uint64_t plain_modulus)
    {
        __int128 sum = 0;
        for (const auto &row : rows)
        {
            const auto a = hebench::centered_mod(row.a, plain_modulus);
            const auto b = hebench::centered_mod(row.b, plain_modulus);
            sum += static_cast<__int128>(a) * static_cast<__int128>(b);
            sum = centered_mod_wide(sum, plain_modulus);
        }
        return centered_mod_wide(sum, plain_modulus);
    }

    std::vector<std::uint64_t> encode_values(
        const std::vector<hebench::ExactRow> &rows,
        bool use_b,
        std::size_t slot_count,
        std::uint64_t plain_modulus)
    {
        std::vector<std::uint64_t> values(slot_count, 0ULL);
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto raw = use_b ? rows[i].b : rows[i].a;
            const auto centered = hebench::centered_mod(raw, plain_modulus);
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

    bool compare_input_slots(
        const std::vector<std::uint64_t> &actual,
        const std::vector<hebench::ExactRow> &rows,
        std::uint64_t plain_modulus,
        std::string &error)
    {
        if (actual.size() < rows.size())
        {
            error = "decoded slot count is smaller than corpus row count";
            return false;
        }
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto expected = hebench::centered_mod(rows[i].a, plain_modulus);
            const auto decoded = hebench::centered_mod_u64(actual[i], plain_modulus);
            if (decoded != expected)
            {
                error = "slot " + std::to_string(i) + " expected " + std::to_string(expected) +
                    " got " + std::to_string(decoded);
                return false;
            }
        }
        error.clear();
        return true;
    }

    std::size_t serialized_size(const seal::Ciphertext &ciphertext)
    {
        std::stringstream stream;
        ciphertext.save(stream);
        return stream.str().size();
    }

    seal::Ciphertext dot_product_pt(
        seal::Evaluator &evaluator,
        const seal::Ciphertext &encrypted_a,
        const seal::Plaintext &plain_b,
        const seal::GaloisKeys &galois_keys,
        std::size_t active_slots,
        std::size_t row_size)
    {
        seal::Ciphertext result;
        evaluator.multiply_plain(encrypted_a, plain_b, result);

        // BFV batching is two rows. rotate_rows reduces each row independently;
        // rotate_columns then swaps the two row sums so slot 0 contains the
        // full-vector dot product, not just the first batching row.
        for (std::size_t step = 1; step < row_size; step *= 2)
        {
            seal::Ciphertext rotated;
            evaluator.rotate_rows(result, static_cast<int>(step), galois_keys, rotated);
            evaluator.add_inplace(result, rotated);
        }
        if (active_slots > row_size)
        {
            seal::Ciphertext swapped;
            evaluator.rotate_columns(result, galois_keys, swapped);
            evaluator.add_inplace(result, swapped);
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

        const auto rows = hebench::read_exact_csv(args.corpus_path);
        if (rows.empty())
        {
            throw std::runtime_error("corpus has no rows: " + args.corpus_path);
        }
        if ((rows.size() & (rows.size() - 1)) != 0)
        {
            throw std::runtime_error("throughput dot product requires a power-of-two row count");
        }

        seal::EncryptionParameters parms(kSchemeType);
        parms.set_poly_modulus_degree(args.ring_size);
        parms.set_coeff_modulus(seal::CoeffModulus::BFVDefault(args.ring_size));
        parms.set_plain_modulus(seal::PlainModulus::Batching(args.ring_size, kPlainModulusBitSize));

        seal::SEALContext context(parms);
        if (!context.parameters_set())
        {
            throw std::runtime_error(context.parameter_error_message());
        }

        seal::BatchEncoder encoder(context);
        const auto slot_capacity = encoder.slot_count();
        const auto row_size = slot_capacity / 2;
        const auto plain_modulus = parms.plain_modulus().value();
        if (rows.size() > slot_capacity)
        {
            throw std::runtime_error("corpus row count exceeds SEAL exact-scheme slot count");
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
        encoder.encode(encode_values(rows, false, slot_capacity, plain_modulus), plain_a);
        encoder.encode(encode_values(rows, true, slot_capacity, plain_modulus), plain_b);

        seal::Ciphertext encrypted_a;
        encryptor.encrypt(plain_a, encrypted_a);

        bool all_correct = true;

        seal::Ciphertext sampled_encrypt;
        const auto encrypt_result = hebench::run_for_duration(args.duration_ms, [&] {
            encryptor.encrypt(plain_a, sampled_encrypt);
        });
        std::string error;
        auto decoded = decrypt_decode(decryptor, encoder, sampled_encrypt);
        auto correct = compare_input_slots(decoded, rows, plain_modulus, error);
        all_correct = correct && all_correct;
        hebench::print_throughput_row(
            kLibraryName,
            kSchemeName,
            "throughput_encrypt",
            rows.size(),
            args.ring_size,
            correct,
            hebench::throughput_base_extra(encrypt_result, args.duration_ms, rows.size(), slot_capacity),
            error);

        seal::Ciphertext sampled_mul;
        const auto mul_result = hebench::run_for_duration(args.duration_ms, [&] {
            evaluator.multiply_plain(encrypted_a, plain_b, sampled_mul);
        });
        decoded = decrypt_decode(decryptor, encoder, sampled_mul);
        correct = hebench::compare_exact_slots(decoded, rows, hebench::ExactOperation::mul, plain_modulus, error);
        all_correct = correct && all_correct;
        hebench::print_throughput_row(
            kLibraryName,
            kSchemeName,
            "throughput_mul_ct_pt",
            rows.size(),
            args.ring_size,
            correct,
            hebench::throughput_base_extra(mul_result, args.duration_ms, rows.size(), slot_capacity),
            error);

        seal::Ciphertext sampled_dot;
        const auto dot_result = hebench::run_for_duration(args.duration_ms, [&] {
            sampled_dot = dot_product_pt(evaluator, encrypted_a, plain_b, galois_keys, rows.size(), row_size);
        });
        decoded = decrypt_decode(decryptor, encoder, sampled_dot);
        const auto actual_dot = decoded.empty() ? 0 : hebench::centered_mod_u64(decoded[0], plain_modulus);
        const auto expected = expected_dot(rows, plain_modulus);
        correct = actual_dot == expected;
        error = correct ? "" : "expected " + std::to_string(expected) + " got " + std::to_string(actual_dot);
        all_correct = correct && all_correct;
        auto dot_extra = hebench::throughput_base_extra(dot_result, args.duration_ms, rows.size(), slot_capacity);
        dot_extra += ",completed_requests=" + std::to_string(dot_result.completed);
        dot_extra += ",requests_per_sec=" + std::to_string(hebench::safe_rate(dot_result.completed, dot_result.elapsed_seconds));
        dot_extra += ",input_values_per_sec=" + std::to_string(
            hebench::safe_rate(dot_result.completed, dot_result.elapsed_seconds) * static_cast<double>(rows.size()));
        dot_extra += ",expected=" + std::to_string(expected) + ",actual=" + std::to_string(actual_dot);
        hebench::print_throughput_row(
            kLibraryName,
            kSchemeName,
            "throughput_dot_product_pt",
            rows.size(),
            args.ring_size,
            correct,
            dot_extra,
            error);

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
        correct = compare_input_slots(decoded, rows, plain_modulus, error);
        all_correct = correct && all_correct;
        auto serialize_extra = hebench::throughput_base_extra(serialize_result, args.duration_ms, rows.size(), slot_capacity);
        serialize_extra += ",total_serialized_bytes=" + std::to_string(total_bytes);
        serialize_extra += ",objects_per_sec=" + std::to_string(hebench::safe_rate(serialize_result.completed, serialize_result.elapsed_seconds));
        serialize_extra += ",mb_per_sec=" + std::to_string(
            serialize_result.elapsed_seconds > 0.0
                ? static_cast<double>(total_bytes) / serialize_result.elapsed_seconds / 1000000.0
                : 0.0);
        serialize_extra += ",sample_byte_size=" + std::to_string(serialized_size(encrypted_a));
        hebench::print_throughput_row(
            kLibraryName,
            kSchemeName,
            "throughput_serialize_ciphertext",
            rows.size(),
            args.ring_size,
            correct,
            serialize_extra,
            error);

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << kBinaryName << " failed: " << error.what() << '\n';
        return 2;
    }
}
