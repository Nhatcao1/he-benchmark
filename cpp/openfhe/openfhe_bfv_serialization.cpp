#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "openfhe.h"
#include "ciphertext-ser.h"
#include "cryptocontext-ser.h"
#include "key/key-ser.h"
#ifdef HE_BENCHMARK_OPENFHE_SERIALIZATION_BGV
#include "scheme/bgvrns/bgvrns-ser.h"
#else
#include "scheme/bfvrns/bfvrns-ser.h"
#endif

#include "benchmark_args.hpp"
#include "csv_reader.hpp"
#include "exact_compare.hpp"
#include "timer.hpp"

namespace
{
    constexpr std::uint64_t kPlainModulus = 786433;
    constexpr std::uint32_t kMultiplicativeDepth = 2;

#ifdef HE_BENCHMARK_OPENFHE_SERIALIZATION_BGV
    constexpr const char *kSchemeName = "BGV";
    constexpr const char *kBinaryName = "openfhe_bgv_serialization";
#else
    constexpr const char *kSchemeName = "BFV";
    constexpr const char *kBinaryName = "openfhe_bfv_serialization";
#endif

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

    template <typename T>
    double timed_serialize(const T &value, std::string &bytes)
    {
        std::stringstream stream;
        const hebench::Timer timer;
        lbcrypto::Serial::Serialize(value, stream, lbcrypto::SerType::BINARY);
        const auto elapsed_ms = timer.elapsed_ms();
        bytes = stream.str();
        return elapsed_ms;
    }

    template <typename T>
    double timed_deserialize(const std::string &bytes, T &value)
    {
        std::stringstream stream(bytes);
        const hebench::Timer timer;
        lbcrypto::Serial::Deserialize(value, stream, lbcrypto::SerType::BINARY);
        return timer.elapsed_ms();
    }

    template <typename SerializeFn>
    double timed_context_key_serialize(SerializeFn serialize_fn, std::string &bytes)
    {
        std::stringstream stream;
        const hebench::Timer timer;
        if (!serialize_fn(stream))
        {
            throw std::runtime_error("OpenFHE context key serialization returned false");
        }
        const auto elapsed_ms = timer.elapsed_ms();
        bytes = stream.str();
        return elapsed_ms;
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
            << "library=OpenFHE"
            << ",scheme=" << kSchemeName
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
        if (rows.size() > args.ring_size)
        {
            throw std::runtime_error(std::string("corpus row count exceeds OpenFHE ") + kSchemeName + " batch size");
        }

#ifdef HE_BENCHMARK_OPENFHE_SERIALIZATION_BGV
        lbcrypto::CCParams<lbcrypto::CryptoContextBGVRNS> parameters;
#else
        lbcrypto::CCParams<lbcrypto::CryptoContextBFVRNS> parameters;
#endif
        parameters.SetPlaintextModulus(kPlainModulus);
        parameters.SetMultiplicativeDepth(kMultiplicativeDepth);
        parameters.SetSecurityLevel(lbcrypto::HEStd_128_classic);
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size));

        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);
        crypto_context->Enable(lbcrypto::ADVANCEDSHE);

        auto keys = crypto_context->KeyGen();
        crypto_context->EvalMultKeyGen(keys.secretKey);
        crypto_context->EvalRotateKeyGen(keys.secretKey, {1, -1, 8});

        const auto plain_a = crypto_context->MakePackedPlaintext(
            encode_signed_inputs(rows, false, args.ring_size, kPlainModulus));
        const auto encrypted_a = crypto_context->Encrypt(keys.publicKey, plain_a);

        bool all_correct = true;
        std::string bytes;
        std::string error;

        auto serialize_ms = timed_serialize(encrypted_a, bytes);
        print_row("serialize_ciphertext", rows.size(), args.ring_size, true, serialize_ms, bytes.size());
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> loaded_ciphertext;
        auto deserialize_ms = timed_deserialize(bytes, loaded_ciphertext);
        const auto decoded_ciphertext = decrypt_decode(crypto_context, keys.secretKey, loaded_ciphertext, kPlainModulus);
        auto correct = compare_input_slots(decoded_ciphertext, rows, false, kPlainModulus, error);
        print_row("deserialize_ciphertext", rows.size(), args.ring_size, correct, deserialize_ms, bytes.size(), error);
        all_correct = correct && all_correct;

        serialize_ms = timed_serialize(keys.secretKey, bytes);
        print_row("serialize_secret_key", rows.size(), args.ring_size, true, serialize_ms, bytes.size());
        lbcrypto::PrivateKey<lbcrypto::DCRTPoly> loaded_secret_key;
        deserialize_ms = timed_deserialize(bytes, loaded_secret_key);
        const auto decoded_secret = decrypt_decode(crypto_context, loaded_secret_key, encrypted_a, kPlainModulus);
        correct = compare_input_slots(decoded_secret, rows, false, kPlainModulus, error);
        print_row("deserialize_secret_key", rows.size(), args.ring_size, correct, deserialize_ms, bytes.size(), error);
        all_correct = correct && all_correct;

        serialize_ms = timed_serialize(keys.publicKey, bytes);
        print_row("serialize_public_key", rows.size(), args.ring_size, true, serialize_ms, bytes.size());
        lbcrypto::PublicKey<lbcrypto::DCRTPoly> loaded_public_key;
        deserialize_ms = timed_deserialize(bytes, loaded_public_key);
        const auto encrypted_with_loaded_public = crypto_context->Encrypt(loaded_public_key, plain_a);
        const auto decoded_public = decrypt_decode(crypto_context, keys.secretKey, encrypted_with_loaded_public, kPlainModulus);
        correct = compare_input_slots(decoded_public, rows, false, kPlainModulus, error);
        print_row("deserialize_public_key", rows.size(), args.ring_size, correct, deserialize_ms, bytes.size(), error);
        all_correct = correct && all_correct;

        serialize_ms = timed_context_key_serialize(
            [&](std::ostream &stream) {
                return crypto_context->SerializeEvalMultKey(stream, lbcrypto::SerType::BINARY);
            },
            bytes);
        print_row("serialize_relin_key", rows.size(), args.ring_size, true, serialize_ms, bytes.size());

        serialize_ms = timed_context_key_serialize(
            [&](std::ostream &stream) {
                return crypto_context->SerializeEvalAutomorphismKey(stream, lbcrypto::SerType::BINARY);
            },
            bytes);
        print_row("serialize_rotation_key", rows.size(), args.ring_size, true, serialize_ms, bytes.size());

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << kBinaryName << " failed: " << error.what() << '\n';
        return 2;
    }
}
