#include <cstdint>
#include <exception>
#include <iostream>
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
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size));

        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);

        auto keys = crypto_context->KeyGen();
        crypto_context->EvalMultKeyGen(keys.secretKey);

        const auto plain_a = crypto_context->MakePackedPlaintext(
            encode_signed_inputs(rows, false, args.ring_size, kPlainModulus));
        const auto plain_b = crypto_context->MakePackedPlaintext(
            encode_signed_inputs(rows, true, args.ring_size, kPlainModulus));

        const auto encrypted_a = crypto_context->Encrypt(keys.publicKey, plain_a);
        const auto encrypted_b = crypto_context->Encrypt(keys.publicKey, plain_b);

        // Keep operation order aligned with the SEAL runner so output files can
        // be diffed or joined by operation name.
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
                crypto_context,
                keys.secretKey,
                rows,
                encrypted_a,
                encrypted_b,
                operation,
                kPlainModulus,
                args.ring_size) && all_correct;
        }

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "openfhe_bfv_exact failed: " << error.what() << '\n';
        return 2;
    }
}
