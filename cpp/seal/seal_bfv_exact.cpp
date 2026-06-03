#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "seal/seal.h"

#include "csv_reader.hpp"
#include "exact_compare.hpp"
#include "timer.hpp"

namespace
{
    using hebench::ExactOperation;

    constexpr std::uint64_t kPlainModulusBitSize = 20;
    constexpr std::size_t kPolyModulusDegree = 8192;

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
        std::uint64_t plain_modulus)
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
        case ExactOperation::square_a:
            evaluator.square(encrypted_a, result);
            evaluator.relinearize_inplace(result, relin_keys);
            break;
        case ExactOperation::negate_a:
            evaluator.negate(encrypted_a, result);
            break;
        }

        const auto elapsed_ms = timer.elapsed_ms();
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
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << elapsed_ms;

        if (!correct)
        {
            std::cout << ",error=\"" << error << "\"";
        }

        std::cout << '\n';
        return correct;
    }
}

int main(int argc, char **argv)
{
    // Optional argv[1] lets run scripts sweep exact_safe_*.csv and
    // exact_edge_cases.csv without recompiling.
    const std::string corpus_path = argc > 1
        ? argv[1]
        : "he_corpus/exact/exact_safe_000008.csv";

    try
    {
        const auto rows = hebench::read_exact_csv(corpus_path);
        if (rows.empty())
        {
            throw std::runtime_error("corpus has no rows: " + corpus_path);
        }

        seal::EncryptionParameters parms(seal::scheme_type::bfv);
        parms.set_poly_modulus_degree(kPolyModulusDegree);
        parms.set_coeff_modulus(seal::CoeffModulus::BFVDefault(kPolyModulusDegree));
        parms.set_plain_modulus(seal::PlainModulus::Batching(kPolyModulusDegree, kPlainModulusBitSize));

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

        seal::KeyGenerator keygen(context);
        const seal::SecretKey secret_key = keygen.secret_key();
        seal::PublicKey public_key;
        seal::RelinKeys relin_keys;
        keygen.create_public_key(public_key);
        keygen.create_relin_keys(relin_keys);

        seal::Encryptor encryptor(context, public_key);
        seal::Evaluator evaluator(context);
        seal::Decryptor decryptor(context, secret_key);

        seal::Plaintext plain_a;
        seal::Plaintext plain_b;
        // All corpus rows are packed into the first rows.size() slots; remaining
        // slots stay zero and are ignored by compare_exact_slots().
        encoder.encode(encode_signed_inputs(rows, false, slot_count, plain_modulus), plain_a);
        encoder.encode(encode_signed_inputs(rows, true, slot_count, plain_modulus), plain_b);

        seal::Ciphertext encrypted_a;
        seal::Ciphertext encrypted_b;
        encryptor.encrypt(plain_a, encrypted_a);
        encryptor.encrypt(plain_b, encrypted_b);

        // Keep operations in a fixed order so result logs are easy to diff
        // across SEAL/OpenFHE and across parameter changes.
        bool all_correct = true;
        for (const auto operation : {
                 ExactOperation::add,
                 ExactOperation::sub,
                 ExactOperation::mul,
                 ExactOperation::square_a,
                 ExactOperation::negate_a,
             })
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
                plain_modulus) && all_correct;
        }

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "seal_bfv_exact failed: " << error.what() << '\n';
        return 2;
    }
}
