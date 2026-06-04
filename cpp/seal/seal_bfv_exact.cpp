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
    using hebench::ExactOperation;

    constexpr std::uint64_t kPlainModulusBitSize = 20;

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
            << ",scheme=BFV"
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

    template <typename T>
    std::size_t serialized_size(const T &value)
    {
        std::stringstream stream;
        value.save(stream);
        return stream.str().size();
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
    // SEAL's BatchEncoder expects unsigned residues. Convert signed corpus
    // values into [0, t) while preserving their centered plaintext meaning.
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

    // Validation is deliberately outside the timed region in run_operation().
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

    bool run_operation(
        seal::Evaluator &evaluator,
        seal::Decryptor &decryptor,
        seal::BatchEncoder &encoder,
        const std::vector<hebench::ExactRow> &rows,
        const seal::Ciphertext &encrypted_a,
        const seal::Ciphertext &encrypted_b,
        const seal::RelinKeys &relin_keys,
        ExactOperation operation,
        std::uint64_t plain_modulus,
        std::size_t ring_size)
    {
        seal::Ciphertext result;
        // Timer starts after inputs are already encrypted so latency reflects
        // only the homomorphic primitive plus required post-processing.
        const hebench::Timer timer;

        switch (operation)
        {
        case ExactOperation::add:
            evaluator.add(encrypted_a, encrypted_b, result);
            break;
        case ExactOperation::sub:
            evaluator.sub(encrypted_a, encrypted_b, result);
            break;
        case ExactOperation::mul:
            evaluator.multiply(encrypted_a, encrypted_b, result);
            evaluator.relinearize_inplace(result, relin_keys);
            break;
        case ExactOperation::add_plain:
        case ExactOperation::sub_plain:
        case ExactOperation::mul_plain:
            throw std::runtime_error("plain operation passed to ciphertext-only runner");
        case ExactOperation::square_a:
            evaluator.square(encrypted_a, result);
            evaluator.relinearize_inplace(result, relin_keys);
            break;
        case ExactOperation::negate_a:
            evaluator.negate(encrypted_a, result);
            break;
        }

        const auto elapsed_ms = timer.elapsed_ms();
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto values_per_sec = ops_per_sec * static_cast<double>(rows.size());
        const auto decoded = decrypt_decode(decryptor, encoder, result);

        // Compare after decrypt/decode to keep benchmark output self-checking.
        // This cost is reported only through correctness, not latency_ms.
        std::string error;
        const bool correct = hebench::compare_exact_slots(decoded, rows, operation, plain_modulus, error);

        std::cout
            << "library=SEAL"
            << ",scheme=BFV"
            << ",operation=" << hebench::operation_name(operation)
            << ",size=" << rows.size()
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << elapsed_ms
            // Throughput is derived from the timed operation latency. values/s
            // counts active packed slots, not the encoder's zero-padded slots.
            << ",ops_per_sec=" << ops_per_sec
            << ",values_per_sec=" << values_per_sec;

        if (!correct)
        {
            std::cout << ",error=\"" << error << "\"";
        }

        std::cout << '\n';
        return correct;
    }

    bool run_plain_operation(
        seal::Evaluator &evaluator,
        seal::Decryptor &decryptor,
        seal::BatchEncoder &encoder,
        const std::vector<hebench::ExactRow> &rows,
        const seal::Ciphertext &encrypted_a,
        const seal::Plaintext &plain_b,
        ExactOperation operation,
        std::uint64_t plain_modulus,
        std::size_t ring_size)
    {
        seal::Ciphertext result;
        const hebench::Timer timer;

        switch (operation)
        {
        case ExactOperation::add_plain:
            evaluator.add_plain(encrypted_a, plain_b, result);
            break;
        case ExactOperation::sub_plain:
            evaluator.sub_plain(encrypted_a, plain_b, result);
            break;
        case ExactOperation::mul_plain:
            evaluator.multiply_plain(encrypted_a, plain_b, result);
            break;
        case ExactOperation::add:
        case ExactOperation::sub:
        case ExactOperation::mul:
        case ExactOperation::square_a:
        case ExactOperation::negate_a:
            throw std::runtime_error("ciphertext operation passed to plaintext runner");
        }

        const auto elapsed_ms = timer.elapsed_ms();
        const auto decoded = decrypt_decode(decryptor, encoder, result);

        std::string error;
        const bool correct = hebench::compare_exact_slots(decoded, rows, operation, plain_modulus, error);
        print_metric_row(
            hebench::operation_name(operation),
            rows.size(),
            ring_size,
            correct,
            elapsed_ms,
            1.0,
            rows.size(),
            serialized_size(result),
            correct ? "" : "error=\"" + error + "\"");
        return correct;
    }
}

int main(int argc, char **argv)
{
    try
    {
        // The Python run_benchmarks.py wrapper passes explicit values here.
        // Defaults keep direct smoke-test execution simple.
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

        // BFV batching capacity is fixed by the parameter set. Larger corpus
        // files need a bigger poly modulus degree or a chunking runner.
        if (rows.size() > slot_count)
        {
            throw std::runtime_error("corpus row count exceeds SEAL BFV slot count");
        }

        const hebench::Timer keygen_timer;
        seal::KeyGenerator keygen(context);
        const seal::SecretKey secret_key = keygen.secret_key();
        seal::PublicKey public_key;
        keygen.create_public_key(public_key);
        const auto keygen_ms = keygen_timer.elapsed_ms();

        const hebench::Timer relin_keygen_timer;
        seal::RelinKeys relin_keys;
        keygen.create_relin_keys(relin_keys);
        const auto relin_keygen_ms = relin_keygen_timer.elapsed_ms();

        print_metric_row(
            "keygen",
            rows.size(),
            args.ring_size,
            true,
            keygen_ms,
            1.0,
            0,
            serialized_size(secret_key) + serialized_size(public_key),
            "secret_key_bytes=" + std::to_string(serialized_size(secret_key)) +
                ",public_key_bytes=" + std::to_string(serialized_size(public_key)));
        print_metric_row(
            "relin_keygen",
            rows.size(),
            args.ring_size,
            true,
            relin_keygen_ms,
            1.0,
            0,
            serialized_size(relin_keys));

        seal::Encryptor encryptor(context, public_key);
        seal::Evaluator evaluator(context);
        seal::Decryptor decryptor(context, secret_key);

        seal::Plaintext plain_a;
        seal::Plaintext plain_b;
        // All corpus rows are packed into the first rows.size() slots; remaining
        // slots stay zero and are ignored by compare_exact_slots().
        const hebench::Timer encode_timer;
        encoder.encode(encode_signed_inputs(rows, false, slot_count, plain_modulus), plain_a);
        encoder.encode(encode_signed_inputs(rows, true, slot_count, plain_modulus), plain_b);
        const auto encode_ms = encode_timer.elapsed_ms();
        print_metric_row(
            "encode",
            rows.size(),
            args.ring_size,
            true,
            encode_ms,
            2.0,
            rows.size() * 2,
            serialized_size(plain_a) + serialized_size(plain_b));

        const hebench::Timer decode_timer;
        std::vector<std::uint64_t> decoded_plain_a;
        std::vector<std::uint64_t> decoded_plain_b;
        encoder.decode(plain_a, decoded_plain_a);
        encoder.decode(plain_b, decoded_plain_b);
        const auto decode_ms = decode_timer.elapsed_ms();
        std::string decode_error;
        const bool decode_correct =
            compare_input_slots(decoded_plain_a, rows, false, plain_modulus, decode_error) &&
            compare_input_slots(decoded_plain_b, rows, true, plain_modulus, decode_error);
        print_metric_row(
            "decode",
            rows.size(),
            args.ring_size,
            decode_correct,
            decode_ms,
            2.0,
            rows.size() * 2,
            0,
            decode_correct ? "" : "error=\"" + decode_error + "\"");

        seal::Ciphertext encrypted_a;
        seal::Ciphertext encrypted_b;
        const hebench::Timer encrypt_timer;
        encryptor.encrypt(plain_a, encrypted_a);
        encryptor.encrypt(plain_b, encrypted_b);
        const auto encrypt_ms = encrypt_timer.elapsed_ms();
        print_metric_row(
            "encrypt",
            rows.size(),
            args.ring_size,
            true,
            encrypt_ms,
            2.0,
            rows.size() * 2,
            serialized_size(encrypted_a) + serialized_size(encrypted_b));

        const hebench::Timer decrypt_timer;
        const auto decrypted_a = decrypt_decode(decryptor, encoder, encrypted_a);
        const auto decrypted_b = decrypt_decode(decryptor, encoder, encrypted_b);
        const auto decrypt_ms = decrypt_timer.elapsed_ms();
        std::string decrypt_error;
        const bool decrypt_correct =
            compare_input_slots(decrypted_a, rows, false, plain_modulus, decrypt_error) &&
            compare_input_slots(decrypted_b, rows, true, plain_modulus, decrypt_error);
        print_metric_row(
            "decrypt",
            rows.size(),
            args.ring_size,
            decrypt_correct,
            decrypt_ms,
            2.0,
            rows.size() * 2,
            0,
            decrypt_correct ? "" : "error=\"" + decrypt_error + "\"");

        // Keep operations in a fixed order so result logs are easy to diff
        // across SEAL/OpenFHE and across parameter changes.
        bool all_correct = true;
        for (const auto operation : {
                 ExactOperation::add,
                 ExactOperation::sub,
                 ExactOperation::mul,
                 ExactOperation::add_plain,
                 ExactOperation::sub_plain,
                 ExactOperation::mul_plain,
                 ExactOperation::square_a,
                 ExactOperation::negate_a,
             })
        {
            if (operation == ExactOperation::add_plain ||
                operation == ExactOperation::sub_plain ||
                operation == ExactOperation::mul_plain)
            {
                all_correct = run_plain_operation(
                    evaluator,
                    decryptor,
                    encoder,
                    rows,
                    encrypted_a,
                    plain_b,
                    operation,
                    plain_modulus,
                    args.ring_size) && all_correct;
            }
            else
            {
                all_correct = run_operation(
                    evaluator,
                    decryptor,
                    encoder,
                    rows,
                    encrypted_a,
                    encrypted_b,
                    relin_keys,
                    operation,
                    plain_modulus,
                    args.ring_size) && all_correct;
            }
        }

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "seal_bfv_exact failed: " << error.what() << '\n';
        return 2;
    }
}
