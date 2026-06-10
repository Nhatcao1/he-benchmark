#include <cstdint>
#include <exception>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "openfhe.h"
#include "ciphertext-ser.h"
#include "cryptocontext-ser.h"
#include "key/key-ser.h"
#include "scheme/bfvrns/bfvrns-ser.h"

#include "benchmark_args.hpp"
#include "csv_reader.hpp"
#include "exact_compare.hpp"
#include "throughput.hpp"

namespace
{
    constexpr std::uint64_t kPlainModulus = 786433;
    constexpr std::uint32_t kMultiplicativeDepth = 2;
    constexpr const char *kLibraryName = "OpenFHE";
    constexpr const char *kSchemeName = "BFV";

    std::vector<int64_t> encode_values(
        const std::vector<hebench::ExactRow> &rows,
        bool use_b,
        std::size_t slot_count)
    {
        std::vector<int64_t> values(slot_count, 0);
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            values[i] = hebench::centered_mod(use_b ? rows[i].b : rows[i].a, kPlainModulus);
        }
        return values;
    }

    std::vector<std::uint64_t> decrypt_decode(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &private_key,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &ciphertext)
    {
        lbcrypto::Plaintext plaintext;
        crypto_context->Decrypt(private_key, ciphertext, &plaintext);
        const auto &packed = plaintext->GetPackedValue();

        std::vector<std::uint64_t> decoded;
        decoded.reserve(packed.size());
        for (const auto value : packed)
        {
            const auto centered = hebench::centered_mod(value, kPlainModulus);
            decoded.push_back(centered < 0
                ? static_cast<std::uint64_t>(centered + static_cast<std::int64_t>(kPlainModulus))
                : static_cast<std::uint64_t>(centered));
        }
        return decoded;
    }

    bool compare_input_slots(
        const std::vector<std::uint64_t> &actual,
        const std::vector<hebench::ExactRow> &rows,
        std::string &error)
    {
        if (actual.size() < rows.size())
        {
            error = "decoded slot count is smaller than corpus row count";
            return false;
        }
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto expected = hebench::centered_mod(rows[i].a, kPlainModulus);
            const auto decoded = hebench::centered_mod_u64(actual[i], kPlainModulus);
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

    std::int64_t centered_mod_wide(__int128 value)
    {
        const auto modulus = static_cast<__int128>(kPlainModulus);
        auto reduced = value % modulus;
        if (reduced < 0)
        {
            reduced += modulus;
        }
        if (reduced > modulus / 2)
        {
            reduced -= modulus;
        }
        return static_cast<std::int64_t>(reduced);
    }

    std::int64_t expected_dot(const std::vector<hebench::ExactRow> &rows)
    {
        __int128 sum = 0;
        for (const auto &row : rows)
        {
            const auto a = hebench::centered_mod(row.a, kPlainModulus);
            const auto b = hebench::centered_mod(row.b, kPlainModulus);
            sum += static_cast<__int128>(a) * static_cast<__int128>(b);
            sum = centered_mod_wide(sum);
        }
        return centered_mod_wide(sum);
    }

    std::vector<int32_t> rotation_steps(std::size_t size)
    {
        std::vector<int32_t> steps;
        for (std::size_t step = 1; step < size; step *= 2)
        {
            steps.push_back(static_cast<int32_t>(step));
        }
        return steps;
    }

    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> rotate_sum(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> crypto_context,
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> value,
        std::size_t size)
    {
        for (const auto step : rotation_steps(size))
        {
            value = crypto_context->EvalAdd(value, crypto_context->EvalRotate(value, step));
        }
        return value;
    }

    std::string serialize_to_string(const lbcrypto::Ciphertext<lbcrypto::DCRTPoly> &value)
    {
        std::stringstream stream;
        lbcrypto::Serial::Serialize(value, stream, lbcrypto::SerType::BINARY);
        return stream.str();
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
        if ((rows.size() & (rows.size() - 1)) != 0)
        {
            throw std::runtime_error("throughput dot product requires a power-of-two row count");
        }
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
        crypto_context->Enable(lbcrypto::ADVANCEDSHE);

        auto keys = crypto_context->KeyGen();
        crypto_context->EvalRotateKeyGen(keys.secretKey, rotation_steps(rows.size()));

        const auto plain_a = crypto_context->MakePackedPlaintext(encode_values(rows, false, args.ring_size));
        const auto plain_b = crypto_context->MakePackedPlaintext(encode_values(rows, true, args.ring_size));
        const auto encrypted_a = crypto_context->Encrypt(keys.publicKey, plain_a);

        bool all_correct = true;

        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> sampled_encrypt;
        const auto encrypt_result = hebench::run_for_duration(args.duration_ms, [&] {
            sampled_encrypt = crypto_context->Encrypt(keys.publicKey, plain_a);
        });
        std::string error;
        auto decoded = decrypt_decode(crypto_context, keys.secretKey, sampled_encrypt);
        auto correct = compare_input_slots(decoded, rows, error);
        all_correct = correct && all_correct;
        hebench::print_throughput_row(
            kLibraryName,
            kSchemeName,
            "throughput_encrypt",
            rows.size(),
            args.ring_size,
            correct,
            hebench::throughput_base_extra(encrypt_result, args.duration_ms, rows.size(), args.ring_size),
            error);

        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> sampled_mul;
        const auto mul_result = hebench::run_for_duration(args.duration_ms, [&] {
            sampled_mul = crypto_context->EvalMult(encrypted_a, plain_b);
        });
        decoded = decrypt_decode(crypto_context, keys.secretKey, sampled_mul);
        correct = hebench::compare_exact_slots(decoded, rows, hebench::ExactOperation::mul, kPlainModulus, error);
        all_correct = correct && all_correct;
        hebench::print_throughput_row(
            kLibraryName,
            kSchemeName,
            "throughput_mul_ct_pt",
            rows.size(),
            args.ring_size,
            correct,
            hebench::throughput_base_extra(mul_result, args.duration_ms, rows.size(), args.ring_size),
            error);

        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> sampled_dot;
        const auto dot_result = hebench::run_for_duration(args.duration_ms, [&] {
            sampled_dot = rotate_sum(crypto_context, crypto_context->EvalMult(encrypted_a, plain_b), rows.size());
        });
        decoded = decrypt_decode(crypto_context, keys.secretKey, sampled_dot);
        const auto actual_dot = decoded.empty() ? 0 : hebench::centered_mod_u64(decoded[0], kPlainModulus);
        const auto expected = expected_dot(rows);
        correct = actual_dot == expected;
        error = correct ? "" : "expected " + std::to_string(expected) + " got " + std::to_string(actual_dot);
        all_correct = correct && all_correct;
        auto dot_extra = hebench::throughput_base_extra(dot_result, args.duration_ms, rows.size(), args.ring_size);
        dot_extra += ",completed_requests=" + std::to_string(dot_result.completed);
        dot_extra += ",requests_per_sec=" + std::to_string(hebench::safe_rate(dot_result.completed, dot_result.elapsed_seconds));
        dot_extra += ",input_values_per_sec=" + std::to_string(
            hebench::safe_rate(dot_result.completed, dot_result.elapsed_seconds) * static_cast<double>(rows.size()));
        dot_extra += ",expected=" + std::to_string(expected) + ",actual=" + std::to_string(actual_dot);
        hebench::print_throughput_row(
            kLibraryName,
            kSchemeName,
            "throughput_dot_product_pt",
            rows.size(),
            args.ring_size,
            correct,
            dot_extra,
            error);

        std::string sampled_bytes;
        std::uint64_t total_bytes = 0;
        const auto serialize_result = hebench::run_for_duration(args.duration_ms, [&] {
            sampled_bytes = serialize_to_string(encrypted_a);
            total_bytes += sampled_bytes.size();
        });
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> loaded;
        std::stringstream stream(sampled_bytes);
        lbcrypto::Serial::Deserialize(loaded, stream, lbcrypto::SerType::BINARY);
        decoded = decrypt_decode(crypto_context, keys.secretKey, loaded);
        correct = compare_input_slots(decoded, rows, error);
        all_correct = correct && all_correct;
        auto serialize_extra = hebench::throughput_base_extra(serialize_result, args.duration_ms, rows.size(), args.ring_size);
        serialize_extra += ",total_serialized_bytes=" + std::to_string(total_bytes);
        serialize_extra += ",objects_per_sec=" + std::to_string(hebench::safe_rate(serialize_result.completed, serialize_result.elapsed_seconds));
        serialize_extra += ",mb_per_sec=" + std::to_string(
            serialize_result.elapsed_seconds > 0.0
                ? static_cast<double>(total_bytes) / serialize_result.elapsed_seconds / 1000000.0
                : 0.0);
        serialize_extra += ",sample_byte_size=" + std::to_string(sampled_bytes.size());
        hebench::print_throughput_row(
            kLibraryName,
            kSchemeName,
            "throughput_serialize_ciphertext",
            rows.size(),
            args.ring_size,
            correct,
            serialize_extra,
            error);

        return all_correct ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "openfhe_bfv_throughput failed: " << error.what() << '\n';
        return 2;
    }
}
