#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "seal/seal.h"

#include "benchmark_args.hpp"
#include "csv_reader.hpp"
#include "exact_compare.hpp"
#include "timer.hpp"

namespace
{
    constexpr std::uint64_t kPlainModulusBitSize = 20;

    std::vector<std::uint64_t> encode_signed_inputs(
        const std::vector<hebench::ExactRow> &rows,
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

    template <typename T>
    std::string save_to_string(const T &value)
    {
        std::stringstream stream;
        value.save(stream);
        return stream.str();
    }

    template <typename T>
    double timed_save(const T &value, std::string &bytes)
    {
        const hebench::Timer timer;
        bytes = save_to_string(value);
        return timer.elapsed_ms();
    }

    template <typename T>
    double timed_load(const seal::SEALContext &context, const std::string &bytes, T &value)
    {
        std::stringstream stream(bytes);
        const hebench::Timer timer;
        value.load(context, stream);
        return timer.elapsed_ms();
    }

    bool compare_input_slots(
        const std::vector<std::uint64_t> &actual,
        const std::vector<hebench::ExactRow> &rows,
        bool use_b,
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
            const auto expected = hebench::centered_mod(use_b ? rows[i].b : rows[i].a, plain_modulus);
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

    void print_row(
        const std::string &operation,
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        double elapsed_ms,
        std::size_t byte_size,
        const std::string &error = "")
    {
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto mb_per_sec = elapsed_ms > 0.0
            ? (static_cast<double>(byte_size) / 1024.0 / 1024.0) / (elapsed_ms / 1000.0)
            : 0.0;

        std::cout
            << "library=SEAL"
            << ",scheme=BFV"
            << ",operation=" << operation
            << ",size=" << size
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << elapsed_ms
            << ",ops_per_sec=" << ops_per_sec
            << ",values_per_sec=0"
            << ",byte_size=" << byte_size
            << ",mb_per_sec=" << mb_per_sec;
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

        seal::EncryptionParameters parms(seal::scheme_type::bfv);
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
            throw std::runtime_error("corpus row count exceeds SEAL BFV slot count");
        }

        seal::KeyGenerator keygen(context);
        const auto secret_key = keygen.secret_key();
        seal::PublicKey public_key;
        seal::RelinKeys relin_keys;
        seal::GaloisKeys galois_keys;
        keygen.create_public_key(public_key);
        keygen.create_relin_keys(relin_keys);
        keygen.create_galois_keys(std::vector<int>{1, -1, 8}, galois_keys);

        seal::Encryptor encryptor(context, public_key);
        seal::Decryptor decryptor(context, secret_key);

        seal::Plaintext plain_a;
        seal::Plaintext plain_b;
        encoder.encode(encode_signed_inputs(rows, false, slot_count, plain_modulus), plain_a);
        encoder.encode(encode_signed_inputs(rows, true, slot_count, plain_modulus), plain_b);

        seal::Ciphertext encrypted_a;
        seal::Ciphertext encrypted_b;
        encryptor.encrypt(plain_a, encrypted_a);
        encryptor.encrypt(plain_b, encrypted_b);

        bool all_correct = true;
        std::string bytes;
        std::string error;

        auto save_ms = timed_save(encrypted_a, bytes);
        print_row("serialize_ciphertext", rows.size(), args.ring_size, true, save_ms, bytes.size());
        seal::Ciphertext loaded_ciphertext;
        auto load_ms = timed_load(context, bytes, loaded_ciphertext);
        const auto decoded_ciphertext = decrypt_decode(decryptor, encoder, loaded_ciphertext);
        auto correct = compare_input_slots(decoded_ciphertext, rows, false, plain_modulus, error);
        print_row("deserialize_ciphertext", rows.size(), args.ring_size, correct, load_ms, bytes.size(), error);
        all_correct = correct && all_correct;

        save_ms = timed_save(secret_key, bytes);
        print_row("serialize_secret_key", rows.size(), args.ring_size, true, save_ms, bytes.size());
        seal::SecretKey loaded_secret_key;
        load_ms = timed_load(context, bytes, loaded_secret_key);
        seal::Decryptor loaded_decryptor(context, loaded_secret_key);
        const auto decoded_secret = decrypt_decode(loaded_decryptor, encoder, encrypted_a);
        correct = compare_input_slots(decoded_secret, rows, false, plain_modulus, error);
        print_row("deserialize_secret_key", rows.size(), args.ring_size, correct, load_ms, bytes.size(), error);
        all_correct = correct && all_correct;

        save_ms = timed_save(public_key, bytes);
        print_row("serialize_public_key", rows.size(), args.ring_size, true, save_ms, bytes.size());
        seal::PublicKey loaded_public_key;
        load_ms = timed_load(context, bytes, loaded_public_key);
        seal::Encryptor loaded_encryptor(context, loaded_public_key);
        seal::Ciphertext encrypted_with_loaded_public;
        loaded_encryptor.encrypt(plain_a, encrypted_with_loaded_public);
        const auto decoded_public = decrypt_decode(decryptor, encoder, encrypted_with_loaded_public);
        correct = compare_input_slots(decoded_public, rows, false, plain_modulus, error);
        print_row("deserialize_public_key", rows.size(), args.ring_size, correct, load_ms, bytes.size(), error);
        all_correct = correct && all_correct;

        save_ms = timed_save(relin_keys, bytes);
        print_row("serialize_relin_key", rows.size(), args.ring_size, true, save_ms, bytes.size());
        seal::RelinKeys loaded_relin_keys;
        load_ms = timed_load(context, bytes, loaded_relin_keys);
        print_row("deserialize_relin_key", rows.size(), args.ring_size, true, load_ms, bytes.size());

        save_ms = timed_save(galois_keys, bytes);
        print_row("serialize_rotation_key", rows.size(), args.ring_size, true, save_ms, bytes.size());
        seal::GaloisKeys loaded_galois_keys;
        load_ms = timed_load(context, bytes, loaded_galois_keys);
        print_row("deserialize_rotation_key", rows.size(), args.ring_size, true, load_ms, bytes.size());

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "seal_bfv_serialization failed: " << error.what() << '\n';
        return 2;
    }
}
