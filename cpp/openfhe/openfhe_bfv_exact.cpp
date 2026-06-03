#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "openfhe.h"

#include "csv_reader.hpp"
#include "exact_compare.hpp"
#include "timer.hpp"

namespace
{
    using hebench::ExactOperation;

    constexpr std::uint64_t kPlainModulus = 786433;
    constexpr std::uint32_t kMultiplicativeDepth = 2;
    constexpr std::uint32_t kBatchSize = 8192;

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

    std::vector<std::uint64_t> decrypt_decode(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &private_key,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &ciphertext,
        std::uint64_t plain_modulus)
    {
        lbcrypto::Plaintext plaintext;
        crypto_context->Decrypt(private_key, ciphertext, &plaintext);
        plaintext->SetLength(kBatchSize);

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
        std::uint64_t plain_modulus)
    {
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> result;
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
        const auto decoded = decrypt_decode(crypto_context, private_key, result, plain_modulus);

        std::string error;
        const bool correct = hebench::compare_exact_slots(decoded, rows, operation, plain_modulus, error);

        std::cout
            << "library=OpenFHE"
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
        if (rows.size() > kBatchSize)
        {
            throw std::runtime_error("corpus row count exceeds OpenFHE BFV batch size");
        }

        lbcrypto::CCParams<lbcrypto::CryptoContextBFVRNS> parameters;
        parameters.SetPlaintextModulus(kPlainModulus);
        parameters.SetMultiplicativeDepth(kMultiplicativeDepth);
        parameters.SetBatchSize(kBatchSize);

        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);

        auto keys = crypto_context->KeyGen();
        crypto_context->EvalMultKeyGen(keys.secretKey);

        const auto plain_a = crypto_context->MakePackedPlaintext(
            encode_signed_inputs(rows, false, kBatchSize, kPlainModulus));
        const auto plain_b = crypto_context->MakePackedPlaintext(
            encode_signed_inputs(rows, true, kBatchSize, kPlainModulus));

        const auto encrypted_a = crypto_context->Encrypt(keys.publicKey, plain_a);
        const auto encrypted_b = crypto_context->Encrypt(keys.publicKey, plain_b);

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
                kPlainModulus) && all_correct;
        }

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "openfhe_bfv_exact failed: " << error.what() << '\n';
        return 2;
    }
}
