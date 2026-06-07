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
#include "memory_usage.hpp"
#include "timer.hpp"

namespace
{
    using hebench::ExactOperation;

    constexpr std::uint64_t kPlainModulusBitSize = 20;

#ifdef HE_BENCHMARK_SEAL_MEMORY_BGV
    constexpr auto kScheme = seal::scheme_type::bgv;
    constexpr const char *kSchemeName = "BGV";
    constexpr const char *kBinaryName = "seal_bgv_memory";
#else
    constexpr auto kScheme = seal::scheme_type::bfv;
    constexpr const char *kSchemeName = "BFV";
    constexpr const char *kBinaryName = "seal_bfv_memory";
#endif

    template <typename T>
    std::size_t serialized_size(const T &value)
    {
        std::stringstream stream;
        value.save(stream);
        return stream.str().size();
    }

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
            << ",scheme=" << kSchemeName
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

        const auto rows = hebench::read_exact_csv(args.corpus_path);
        if (rows.empty())
        {
            throw std::runtime_error("corpus has no rows: " + args.corpus_path);
        }
        const auto baseline_kb = hebench::peak_rss_kb();

        const hebench::Timer context_timer;
        seal::EncryptionParameters parms(kScheme);
        parms.set_poly_modulus_degree(args.ring_size);
        parms.set_coeff_modulus(seal::CoeffModulus::BFVDefault(args.ring_size));
        parms.set_plain_modulus(seal::PlainModulus::Batching(args.ring_size, kPlainModulusBitSize));
        seal::SEALContext context(parms);
        if (!context.parameters_set())
        {
            throw std::runtime_error(context.parameter_error_message());
        }
        const auto context_ms = context_timer.elapsed_ms();
        print_row("memory_context", rows.size(), args.ring_size, true, context_ms, baseline_kb);

        seal::BatchEncoder encoder(context);
        const auto slot_count = encoder.slot_count();
        const auto plain_modulus = parms.plain_modulus().value();
        if (rows.size() > slot_count)
        {
            throw std::runtime_error("corpus row count exceeds SEAL slot count");
        }

        const hebench::Timer keygen_timer;
        seal::KeyGenerator keygen(context);
        const auto secret_key = keygen.secret_key();
        seal::PublicKey public_key;
        keygen.create_public_key(public_key);
        const auto keygen_ms = keygen_timer.elapsed_ms();
        print_row(
            "memory_keygen",
            rows.size(),
            args.ring_size,
            true,
            keygen_ms,
            baseline_kb,
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
        encoder.encode(encode_signed_inputs(rows, false, slot_count, plain_modulus), plain_a);
        encoder.encode(encode_signed_inputs(rows, true, slot_count, plain_modulus), plain_b);
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
        const auto multiply_ms = multiply_timer.elapsed_ms();
        auto decoded = decrypt_decode(decryptor, encoder, product);
        std::string error;
        auto correct = hebench::compare_exact_slots(decoded, rows, ExactOperation::mul, plain_modulus, error);
        print_row(
            "memory_multiply",
            rows.size(),
            args.ring_size,
            correct,
            multiply_ms,
            baseline_kb,
            serialized_size(product),
            "components_after=" + std::to_string(product.size()) + (correct ? "" : ",error=\"" + error + "\""));

        seal::Ciphertext rotated;
        const hebench::Timer rotate_timer;
        evaluator.rotate_rows(encrypted_a, 1, galois_keys, rotated);
        const auto rotate_ms = rotate_timer.elapsed_ms();
        print_row("memory_rotate", rows.size(), args.ring_size, true, rotate_ms, baseline_kb, serialized_size(rotated), "rotation_step=1");

        const hebench::Timer decrypt_timer;
        decoded = decrypt_decode(decryptor, encoder, encrypted_a);
        const auto decrypt_ms = decrypt_timer.elapsed_ms();
        correct = decoded.size() >= rows.size();
        print_row("memory_decrypt", rows.size(), args.ring_size, correct, decrypt_ms, baseline_kb);

        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << kBinaryName << " failed: " << error.what() << '\n';
        return 2;
    }
}
