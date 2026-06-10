#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "openfhe.h"

#include "benchmark_args.hpp"
#include "csv_reader.hpp"
#include "exact_compare.hpp"
#include "timer.hpp"

namespace
{
    using hebench::ExactOperation;

    constexpr std::uint64_t kPlainModulus = 786433;
    constexpr std::uint32_t kMultiplicativeDepth = 2;

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
            << "library=OpenFHE"
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

    // OpenFHE packed plaintexts accept signed slot values directly. Normalize
    // corpus inputs to the same centered interval used for comparison.
    std::vector<int64_t> encode_signed_inputs(
        const std::vector<hebench::ExactRow> &rows,
        bool use_b,
        std::size_t slot_count,
        std::uint64_t plain_modulus)
    {
        std::vector<int64_t> values(slot_count, 0);

        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto value = use_b ? rows[i].b : rows[i].a;
            values[i] = hebench::centered_mod(value, plain_modulus);
        }

        return values;
    }

    // Convert OpenFHE's signed packed result back into unsigned residues so the
    // shared compare_exact_slots() path can be reused with SEAL.
    std::vector<std::uint64_t> decrypt_decode(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &private_key,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &ciphertext,
        std::uint64_t plain_modulus)
    {
        lbcrypto::Plaintext plaintext;
        crypto_context->Decrypt(private_key, ciphertext, &plaintext);
        // The packed length is fixed by the benchmark's selected ring size.
        // OpenFHE may return more slots internally, but compare_exact_slots()
        // only checks the corpus-sized prefix.

        const auto &packed = plaintext->GetPackedValue();
        std::vector<std::uint64_t> decoded;
        decoded.reserve(packed.size());

        for (const auto value : packed)
        {
            const auto centered = hebench::centered_mod(value, plain_modulus);
            decoded.push_back(centered < 0
                ? static_cast<std::uint64_t>(centered + static_cast<std::int64_t>(plain_modulus))
                : static_cast<std::uint64_t>(centered));
        }

        return decoded;
    }

    bool run_operation(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &private_key,
        const std::vector<hebench::ExactRow> &rows,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &encrypted_a,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &encrypted_b,
        ExactOperation operation,
        std::uint64_t plain_modulus,
        std::size_t ring_size)
    {
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> result;
        // Timer starts after encryption/key generation so latency reports only
        // the homomorphic primitive being benchmarked.
        const hebench::Timer timer;

        switch (operation)
        {
        case ExactOperation::add:
            result = crypto_context->EvalAdd(encrypted_a, encrypted_b);
            break;
        case ExactOperation::sub:
            result = crypto_context->EvalSub(encrypted_a, encrypted_b);
            break;
        case ExactOperation::mul:
            result = crypto_context->EvalMult(encrypted_a, encrypted_b);
            break;
        case ExactOperation::add_plain:
        case ExactOperation::sub_plain:
        case ExactOperation::mul_plain:
            throw std::runtime_error("plain operation passed to ciphertext-only runner");
        case ExactOperation::square_a:
            result = crypto_context->EvalMult(encrypted_a, encrypted_a);
            break;
        case ExactOperation::negate_a:
            result = crypto_context->EvalNegate(encrypted_a);
            break;
        }

        const auto elapsed_ms = timer.elapsed_ms();
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto values_per_sec = ops_per_sec * static_cast<double>(rows.size());
        const auto decoded = decrypt_decode(crypto_context, private_key, result, plain_modulus);

        // Correctness checking stays outside latency_ms but keeps each result
        // line self-validating.
        std::string error;
        const bool correct = hebench::compare_exact_slots(decoded, rows, operation, plain_modulus, error);

        std::cout
            << "library=OpenFHE"
            << ",scheme=BFV"
            << ",operation=" << hebench::operation_name(operation)
            << ",size=" << rows.size()
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << elapsed_ms
            // Throughput is derived from the timed operation latency. values/s
            // counts active packed slots, not the zero-padded batch capacity.
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
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &private_key,
        const std::vector<hebench::ExactRow> &rows,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &encrypted_a,
        lbcrypto::Plaintext plain_b,
        ExactOperation operation,
        std::uint64_t plain_modulus,
        std::size_t ring_size)
    {
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> result;
        const hebench::Timer timer;

        switch (operation)
        {
        case ExactOperation::add_plain:
            result = crypto_context->EvalAdd(encrypted_a, plain_b);
            break;
        case ExactOperation::sub_plain:
            result = crypto_context->EvalSub(encrypted_a, plain_b);
            break;
        case ExactOperation::mul_plain:
            result = crypto_context->EvalMult(encrypted_a, plain_b);
            break;
        case ExactOperation::add:
        case ExactOperation::sub:
        case ExactOperation::mul:
        case ExactOperation::square_a:
        case ExactOperation::negate_a:
            throw std::runtime_error("ciphertext operation passed to plaintext runner");
        }

        const auto elapsed_ms = timer.elapsed_ms();
        const auto decoded = decrypt_decode(crypto_context, private_key, result, plain_modulus);

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
            0,
            correct ? "" : "error=\"" + error + "\"");
        return correct;
    }

    bool run_relinearization(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &private_key,
        const std::vector<hebench::ExactRow> &rows,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &encrypted_a,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &encrypted_b,
        std::uint64_t plain_modulus,
        std::size_t ring_size)
    {
        const auto product = crypto_context->EvalMultNoRelin(encrypted_a, encrypted_b);
        const auto components_before = product->NumberCiphertextElements();

        const hebench::Timer timer;
        const auto relinearized = crypto_context->Relinearize(product);
        const auto elapsed_ms = timer.elapsed_ms();

        const auto components_after = relinearized->NumberCiphertextElements();
        const auto reduction_ratio = components_before > 0
            ? static_cast<double>(components_after) / static_cast<double>(components_before)
            : 0.0;
        const auto decoded = decrypt_decode(crypto_context, private_key, relinearized, plain_modulus);

        std::string error;
        const bool correct = hebench::compare_exact_slots(decoded, rows, ExactOperation::mul, plain_modulus, error);
        print_metric_row(
            "relin",
            rows.size(),
            ring_size,
            correct,
            elapsed_ms,
            1.0,
            rows.size(),
            0,
            "components_before=" + std::to_string(components_before) +
                ",components_after=" + std::to_string(components_after) +
                ",reduction_ratio=" + std::to_string(reduction_ratio) +
                (correct ? "" : ",error=\"" + error + "\""));
        return correct;
    }

    bool run_mod_switch(
        const std::vector<hebench::ExactRow> &rows,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &encrypted_a,
        std::size_t ring_size)
    {
        print_metric_row(
            "mod_switch",
            rows.size(),
            ring_size,
            true,
            0.0,
            0.0,
            rows.size(),
            0,
            "supported=false,reason=\"OpenFHE ModReduce/LevelReduce are documented for BGV/CKKS, not BFV\"" +
                std::string(",level_before=") + std::to_string(encrypted_a->GetLevel()) +
                ",level_after=" + std::to_string(encrypted_a->GetLevel()) +
                ",levels_dropped=0" +
                ",components_after=" + std::to_string(encrypted_a->NumberCiphertextElements()));
        return true;
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
        // The OpenFHE parameter set fixes the packed vector size. Larger corpus
        // files need a larger batch size or a chunking runner.
        if (rows.size() > args.ring_size)
        {
            throw std::runtime_error("corpus row count exceeds OpenFHE BFV batch size");
        }

        lbcrypto::CCParams<lbcrypto::CryptoContextBFVRNS> parameters;
        parameters.SetPlaintextModulus(kPlainModulus);
        parameters.SetMultiplicativeDepth(kMultiplicativeDepth);
        parameters.SetSecurityLevel(lbcrypto::HEStd_128_classic);
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size));

        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);

        const hebench::Timer keygen_timer;
        auto keys = crypto_context->KeyGen();
        const auto keygen_ms = keygen_timer.elapsed_ms();

        const hebench::Timer relin_keygen_timer;
        crypto_context->EvalMultKeyGen(keys.secretKey);
        const auto relin_keygen_ms = relin_keygen_timer.elapsed_ms();

        print_metric_row(
            "keygen",
            rows.size(),
            args.ring_size,
            true,
            keygen_ms,
            1.0,
            0);
        print_metric_row(
            "relin_keygen",
            rows.size(),
            args.ring_size,
            true,
            relin_keygen_ms,
            1.0,
            0);

        const hebench::Timer encode_timer;
        const auto plain_a = crypto_context->MakePackedPlaintext(
            encode_signed_inputs(rows, false, args.ring_size, kPlainModulus));
        const auto plain_b = crypto_context->MakePackedPlaintext(
            encode_signed_inputs(rows, true, args.ring_size, kPlainModulus));
        const auto encode_ms = encode_timer.elapsed_ms();
        print_metric_row(
            "encode",
            rows.size(),
            args.ring_size,
            true,
            encode_ms,
            2.0,
            rows.size() * 2);

        const hebench::Timer decode_timer;
        const auto &packed_a = plain_a->GetPackedValue();
        const auto &packed_b = plain_b->GetPackedValue();
        std::vector<std::uint64_t> decoded_plain_a;
        std::vector<std::uint64_t> decoded_plain_b;
        decoded_plain_a.reserve(packed_a.size());
        decoded_plain_b.reserve(packed_b.size());
        for (const auto value : packed_a)
        {
            const auto centered = hebench::centered_mod(value, kPlainModulus);
            decoded_plain_a.push_back(centered < 0
                ? static_cast<std::uint64_t>(centered + static_cast<std::int64_t>(kPlainModulus))
                : static_cast<std::uint64_t>(centered));
        }
        for (const auto value : packed_b)
        {
            const auto centered = hebench::centered_mod(value, kPlainModulus);
            decoded_plain_b.push_back(centered < 0
                ? static_cast<std::uint64_t>(centered + static_cast<std::int64_t>(kPlainModulus))
                : static_cast<std::uint64_t>(centered));
        }
        const auto decode_ms = decode_timer.elapsed_ms();
        std::string decode_error;
        const bool decode_correct =
            compare_input_slots(decoded_plain_a, rows, false, kPlainModulus, decode_error) &&
            compare_input_slots(decoded_plain_b, rows, true, kPlainModulus, decode_error);
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

        const hebench::Timer encrypt_timer;
        const auto encrypted_a = crypto_context->Encrypt(keys.publicKey, plain_a);
        const auto encrypted_b = crypto_context->Encrypt(keys.publicKey, plain_b);
        const auto encrypt_ms = encrypt_timer.elapsed_ms();
        print_metric_row(
            "encrypt",
            rows.size(),
            args.ring_size,
            true,
            encrypt_ms,
            2.0,
            rows.size() * 2);

        const hebench::Timer decrypt_timer;
        const auto decrypted_a = decrypt_decode(crypto_context, keys.secretKey, encrypted_a, kPlainModulus);
        const auto decrypted_b = decrypt_decode(crypto_context, keys.secretKey, encrypted_b, kPlainModulus);
        const auto decrypt_ms = decrypt_timer.elapsed_ms();
        std::string decrypt_error;
        const bool decrypt_correct =
            compare_input_slots(decrypted_a, rows, false, kPlainModulus, decrypt_error) &&
            compare_input_slots(decrypted_b, rows, true, kPlainModulus, decrypt_error);
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

        // Keep operation order aligned with the SEAL runner so output files can
        // be diffed or joined by operation name.
        bool all_correct = true;
        all_correct = run_mod_switch(
            rows,
            encrypted_a,
            args.ring_size) && all_correct;

        all_correct = run_relinearization(
            crypto_context,
            keys.secretKey,
            rows,
            encrypted_a,
            encrypted_b,
            kPlainModulus,
            args.ring_size) && all_correct;

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
                    crypto_context,
                    keys.secretKey,
                    rows,
                    encrypted_a,
                    plain_b,
                    operation,
                    kPlainModulus,
                    args.ring_size) && all_correct;
            }
            else
            {
                all_correct = run_operation(
                    crypto_context,
                    keys.secretKey,
                    rows,
                    encrypted_a,
                    encrypted_b,
                    operation,
                    kPlainModulus,
                    args.ring_size) && all_correct;
            }
        }

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "openfhe_bfv_exact failed: " << error.what() << '\n';
        return 2;
    }
}
