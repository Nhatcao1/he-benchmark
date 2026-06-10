#include <cmath>
#include <cstddef>
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
#include "scheme/ckksrns/ckksrns-ser.h"

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
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &private_key,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &ciphertext,
        std::size_t length)
    {
        lbcrypto::Plaintext plaintext;
        crypto_context->Decrypt(private_key, ciphertext, &plaintext);
        plaintext->SetLength(length);
        return plaintext->GetRealPackedValue();
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
        const std::string &extra = "",
        const std::string &error = "")
    {
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto mb_per_sec = elapsed_ms > 0.0
            ? (static_cast<double>(byte_size) / 1024.0 / 1024.0) / (elapsed_ms / 1000.0)
            : 0.0;

        std::cout
            << "library=OpenFHE"
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
        if (rows.size() > args.ring_size / 2)
        {
            throw std::runtime_error("corpus row count exceeds OpenFHE CKKS batch size");
        }

        const auto ckks_config = hebench::ckks_config_for(args, 3, 40);
        g_ckks_config_extra = hebench::ckks_config_extra(ckks_config);

        lbcrypto::CCParams<lbcrypto::CryptoContextCKKSRNS> parameters;
        parameters.SetMultiplicativeDepth(static_cast<std::uint32_t>(ckks_config.multiplicative_depth));
        parameters.SetScalingModSize(static_cast<std::uint32_t>(ckks_config.scale_bits));
        if (ckks_config.explicit_first_mod)
        {
            parameters.SetFirstModSize(static_cast<std::uint32_t>(ckks_config.first_mod_bits));
        }
        if (ckks_config.relaxed_security)
        {
            parameters.SetSecurityLevel(lbcrypto::HEStd_NotSet);
        }
        else
        {
            parameters.SetSecurityLevel(lbcrypto::HEStd_128_classic);
        }
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size / 2));

        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);
        crypto_context->Enable(lbcrypto::ADVANCEDSHE);

        auto keys = crypto_context->KeyGen();
        crypto_context->EvalMultKeyGen(keys.secretKey);
        crypto_context->EvalRotateKeyGen(keys.secretKey, {1, -1, 8});

        auto plain_a = crypto_context->MakeCKKSPackedPlaintext(values_for(rows, false, args.ring_size / 2));
        const auto encrypted_a = crypto_context->Encrypt(keys.publicKey, plain_a);

        bool all_correct = true;
        std::string bytes;
        std::string error;
        const auto cipher_extra = "scale=" + std::to_string(encrypted_a->GetScalingFactor()) +
            ",level=" + std::to_string(encrypted_a->GetLevel());

        auto serialize_ms = timed_serialize(encrypted_a, bytes);
        print_row("serialize_ciphertext", rows.size(), args.ring_size, true, serialize_ms, bytes.size(), cipher_extra);
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> loaded_ciphertext;
        auto deserialize_ms = timed_deserialize(bytes, loaded_ciphertext);
        const auto decoded_ciphertext = decrypt_decode(crypto_context, keys.secretKey, loaded_ciphertext, rows.size());
        auto correct = compare_input(decoded_ciphertext, rows, false, error);
        print_row("deserialize_ciphertext", rows.size(), args.ring_size, correct, deserialize_ms, bytes.size(), cipher_extra, error);
        all_correct = correct && all_correct;

        serialize_ms = timed_serialize(keys.secretKey, bytes);
        print_row("serialize_secret_key", rows.size(), args.ring_size, true, serialize_ms, bytes.size());
        lbcrypto::PrivateKey<lbcrypto::DCRTPoly> loaded_secret_key;
        deserialize_ms = timed_deserialize(bytes, loaded_secret_key);
        const auto decoded_secret = decrypt_decode(crypto_context, loaded_secret_key, encrypted_a, rows.size());
        correct = compare_input(decoded_secret, rows, false, error);
        print_row("deserialize_secret_key", rows.size(), args.ring_size, correct, deserialize_ms, bytes.size(), "", error);
        all_correct = correct && all_correct;

        serialize_ms = timed_serialize(keys.publicKey, bytes);
        print_row("serialize_public_key", rows.size(), args.ring_size, true, serialize_ms, bytes.size());
        lbcrypto::PublicKey<lbcrypto::DCRTPoly> loaded_public_key;
        deserialize_ms = timed_deserialize(bytes, loaded_public_key);
        const auto encrypted_with_loaded_public = crypto_context->Encrypt(loaded_public_key, plain_a);
        const auto decoded_public = decrypt_decode(crypto_context, keys.secretKey, encrypted_with_loaded_public, rows.size());
        correct = compare_input(decoded_public, rows, false, error);
        print_row("deserialize_public_key", rows.size(), args.ring_size, correct, deserialize_ms, bytes.size(), "", error);
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
        std::cerr << "openfhe_ckks_serialization failed: " << error.what() << '\n';
        return 2;
    }
}
