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
#include "timer.hpp"

namespace
{
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

        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto expected = use_b ? rows[i].b : rows[i].a;
            if (std::abs(actual[i] - expected) > 1e-3)
            {
                error = "slot " + std::to_string(i) + " expected " + std::to_string(expected) +
                    " got " + std::to_string(actual[i]);
                return false;
            }
        }

        error.clear();
        return true;
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

    void print_row(
        const std::string &operation,
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        double elapsed_ms,
        std::size_t byte_size,
        const std::string &extra = "",
        const std::string &error = "")
    {
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto mb_per_sec = elapsed_ms > 0.0
            ? (static_cast<double>(byte_size) / 1024.0 / 1024.0) / (elapsed_ms / 1000.0)
            : 0.0;

        std::cout
            << "library=SEAL"
            << ",scheme=CKKS"
            << ",operation=" << operation
            << ",size=" << size
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << elapsed_ms
            << ",ops_per_sec=" << ops_per_sec
            << ",values_per_sec=0";
        if (!g_ckks_config_extra.empty())
        {
            std::cout << ',' << g_ckks_config_extra;
        }
        std::cout
            << ",byte_size=" << byte_size
            << ",mb_per_sec=" << mb_per_sec;
        if (!extra.empty())
        {
            std::cout << ',' << extra;
        }
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
        seal::RelinKeys relin_keys;
        seal::GaloisKeys galois_keys;
        keygen.create_public_key(public_key);
        keygen.create_relin_keys(relin_keys);
        keygen.create_galois_keys(std::vector<int>{1, -1, 8}, galois_keys);

        seal::Encryptor encryptor(context, public_key);
        seal::Decryptor decryptor(context, secret_key);

        seal::Plaintext plain_a;
        encoder.encode(values_for(rows, false, slot_count), hebench::ckks_scale(ckks_config), plain_a);

        seal::Ciphertext encrypted_a;
        encryptor.encrypt(plain_a, encrypted_a);

        bool all_correct = true;
        std::string bytes;
        std::string error;
        const auto cipher_extra = "scale=" + std::to_string(encrypted_a.scale()) +
            ",level=" + std::to_string(encrypted_a.coeff_modulus_size());

        auto save_ms = timed_save(encrypted_a, bytes);
        print_row("serialize_ciphertext", rows.size(), args.ring_size, true, save_ms, bytes.size(), cipher_extra);
        seal::Ciphertext loaded_ciphertext;
        auto load_ms = timed_load(context, bytes, loaded_ciphertext);
        const auto decoded_ciphertext = decrypt_decode(decryptor, encoder, loaded_ciphertext);
        auto correct = compare_input(decoded_ciphertext, rows, false, error);
        print_row("deserialize_ciphertext", rows.size(), args.ring_size, correct, load_ms, bytes.size(), cipher_extra, error);
        all_correct = correct && all_correct;

        save_ms = timed_save(secret_key, bytes);
        print_row("serialize_secret_key", rows.size(), args.ring_size, true, save_ms, bytes.size());
        seal::SecretKey loaded_secret_key;
        load_ms = timed_load(context, bytes, loaded_secret_key);
        seal::Decryptor loaded_decryptor(context, loaded_secret_key);
        const auto decoded_secret = decrypt_decode(loaded_decryptor, encoder, encrypted_a);
        correct = compare_input(decoded_secret, rows, false, error);
        print_row("deserialize_secret_key", rows.size(), args.ring_size, correct, load_ms, bytes.size(), "", error);
        all_correct = correct && all_correct;

        save_ms = timed_save(public_key, bytes);
        print_row("serialize_public_key", rows.size(), args.ring_size, true, save_ms, bytes.size());
        seal::PublicKey loaded_public_key;
        load_ms = timed_load(context, bytes, loaded_public_key);
        seal::Encryptor loaded_encryptor(context, loaded_public_key);
        seal::Ciphertext encrypted_with_loaded_public;
        loaded_encryptor.encrypt(plain_a, encrypted_with_loaded_public);
        const auto decoded_public = decrypt_decode(decryptor, encoder, encrypted_with_loaded_public);
        correct = compare_input(decoded_public, rows, false, error);
        print_row("deserialize_public_key", rows.size(), args.ring_size, correct, load_ms, bytes.size(), "", error);
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
        std::cerr << "seal_ckks_serialization failed: " << error.what() << '\n';
        return 2;
    }
}
