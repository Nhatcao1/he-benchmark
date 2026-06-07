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
#include "csv_reader.hpp"
#include "memory_usage.hpp"
#include "timer.hpp"

namespace
{
    constexpr double kScale = static_cast<double>(std::uint64_t{1} << 40);

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

    void print_row(
        const std::string &operation,
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        double elapsed_ms,
        std::size_t baseline_kb,
        std::size_t byte_size = 0,
        const std::string &extra = "")
    {
        const auto peak_kb = hebench::peak_rss_kb();
        const auto delta_kb = peak_kb >= baseline_kb ? peak_kb - baseline_kb : 0;
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;

        std::cout
            << "library=SEAL"
            << ",scheme=CKKS"
            << ",operation=" << operation
            << ",size=" << size
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << elapsed_ms
            << ",ops_per_sec=" << ops_per_sec
            << ",values_per_sec=0"
            << ",peak_rss_kb=" << peak_kb
            << ",delta_peak_rss_kb=" << delta_kb;
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
        const auto baseline_kb = hebench::peak_rss_kb();

        const hebench::Timer context_timer;
        seal::EncryptionParameters parms(seal::scheme_type::ckks);
        parms.set_poly_modulus_degree(args.ring_size);
        parms.set_coeff_modulus(seal::CoeffModulus::Create(args.ring_size, {60, 40, 40, 60}));
        seal::SEALContext context(parms);
        if (!context.parameters_set())
        {
            throw std::runtime_error(context.parameter_error_message());
        }
        const auto context_ms = context_timer.elapsed_ms();
        print_row("memory_context", rows.size(), args.ring_size, true, context_ms, baseline_kb);

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
        print_row("memory_keygen", rows.size(), args.ring_size, true, keygen_ms, baseline_kb,
            serialized_size(secret_key) + serialized_size(public_key));

        const hebench::Timer relin_timer;
        seal::RelinKeys relin_keys;
        keygen.create_relin_keys(relin_keys);
        const auto relin_ms = relin_timer.elapsed_ms();
        print_row("memory_relin_keygen", rows.size(), args.ring_size, true, relin_ms, baseline_kb, serialized_size(relin_keys));

        const hebench::Timer rotation_timer;
        seal::GaloisKeys galois_keys;
        keygen.create_galois_keys(std::vector<int>{1}, galois_keys);
        const auto rotation_ms = rotation_timer.elapsed_ms();
        print_row("memory_rotation_keygen", rows.size(), args.ring_size, true, rotation_ms, baseline_kb, serialized_size(galois_keys));

        seal::Plaintext plain_a;
        seal::Plaintext plain_b;
        const hebench::Timer encode_timer;
        encoder.encode(values_for(rows, false, slot_count), kScale, plain_a);
        encoder.encode(values_for(rows, true, slot_count), kScale, plain_b);
        const auto encode_ms = encode_timer.elapsed_ms();
        print_row("memory_encode", rows.size(), args.ring_size, true, encode_ms, baseline_kb, serialized_size(plain_a) + serialized_size(plain_b));

        seal::Encryptor encryptor(context, public_key);
        seal::Evaluator evaluator(context);
        seal::Decryptor decryptor(context, secret_key);

        seal::Ciphertext encrypted_a;
        seal::Ciphertext encrypted_b;
        const hebench::Timer encrypt_timer;
        encryptor.encrypt(plain_a, encrypted_a);
        encryptor.encrypt(plain_b, encrypted_b);
        const auto encrypt_ms = encrypt_timer.elapsed_ms();
        print_row("memory_encrypt", rows.size(), args.ring_size, true, encrypt_ms, baseline_kb, serialized_size(encrypted_a) + serialized_size(encrypted_b));

        seal::Ciphertext product;
        const hebench::Timer multiply_timer;
        evaluator.multiply(encrypted_a, encrypted_b, product);
        evaluator.relinearize_inplace(product, relin_keys);
        evaluator.rescale_to_next_inplace(product);
        const auto multiply_ms = multiply_timer.elapsed_ms();
        const auto decoded = decrypt_decode(decryptor, encoder, product);
        const auto metrics = hebench::compare_ckks_slots(decoded, rows, hebench::CkksOperation::mul);
        print_row(
            "memory_multiply",
            rows.size(),
            args.ring_size,
            metrics.correct,
            multiply_ms,
            baseline_kb,
            serialized_size(product),
            "components_after=" + std::to_string(product.size()) +
                ",scale_after=" + std::to_string(product.scale()) +
                ",level_after=" + std::to_string(product.coeff_modulus_size()) +
                ",mae=" + std::to_string(metrics.mae) +
                ",precision_bits=" + std::to_string(metrics.precision_bits) +
                (metrics.correct ? "" : ",error=\"" + metrics.error + "\""));

        seal::Ciphertext rotated;
        const hebench::Timer rotate_timer;
        evaluator.rotate_vector(encrypted_a, 1, galois_keys, rotated);
        const auto rotate_ms = rotate_timer.elapsed_ms();
        print_row("memory_rotate", rows.size(), args.ring_size, true, rotate_ms, baseline_kb, serialized_size(rotated), "rotation_step=1");

        const hebench::Timer decrypt_timer;
        const auto decrypted = decrypt_decode(decryptor, encoder, encrypted_a);
        const auto decrypt_ms = decrypt_timer.elapsed_ms();
        print_row("memory_decrypt", rows.size(), args.ring_size, decrypted.size() >= rows.size(), decrypt_ms, baseline_kb);

        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "seal_ckks_memory failed: " << error.what() << '\n';
        return 2;
    }
}
