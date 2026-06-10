#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#ifdef HE_BENCHMARK_RESOURCE_HEAP
#include "allocation_tracker.hpp"
#endif
#include "openfhe.h"

#include "benchmark_args.hpp"
#include "csv_reader.hpp"
#include "exact_compare.hpp"
#include "memory_usage.hpp"
#include "timer.hpp"

namespace
{
    constexpr std::uint64_t kPlainModulus = 786433;

#ifdef HE_BENCHMARK_OPENFHE_RESOURCE_BGV
    constexpr const char *kSchemeName = "BGV";
#else
    constexpr const char *kSchemeName = "BFV";
#endif

#ifdef HE_BENCHMARK_RESOURCE_HEAP
    constexpr const char *kModeName = "heap";
#elif defined(HE_BENCHMARK_RESOURCE_FOOTPRINT)
    constexpr const char *kModeName = "footprint";
#elif defined(HE_BENCHMARK_RESOURCE_CORPUS_MEMORY)
    constexpr const char *kModeName = "corpus_memory";
#elif defined(HE_BENCHMARK_RESOURCE_THREAD_MEMORY)
    constexpr const char *kModeName = "thread_memory";
#else
    constexpr const char *kModeName = "cpu";
#endif

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

    void print_row(
        const std::string &operation,
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        double wall_ms,
        double cpu_before,
        std::size_t baseline_kb,
        const std::string &extra = "")
    {
        const auto cpu_after = hebench::process_cpu_seconds();
        const auto cpu_ms = std::max(0.0, (cpu_after - cpu_before) * 1000.0);
        const auto cpu_utilization_pct = wall_ms > 0.0 ? (cpu_ms / wall_ms) * 100.0 : 0.0;
        const auto peak_kb = hebench::peak_rss_kb();
        const auto delta_kb = peak_kb >= baseline_kb ? peak_kb - baseline_kb : 0;
        const auto ops_per_sec = wall_ms > 0.0 ? 1000.0 / wall_ms : 0.0;
        const auto values_per_sec = wall_ms > 0.0 ? static_cast<double>(size) * 1000.0 / wall_ms : 0.0;

        std::cout
            << "library=OpenFHE"
            << ",scheme=" << kSchemeName
            << ",operation=" << operation
            << ",size=" << size
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << wall_ms
            << ",ops_per_sec=" << ops_per_sec
            << ",values_per_sec=" << values_per_sec
            << ",resource_mode=" << kModeName
            << ",cpu_ms=" << cpu_ms
            << ",cpu_utilization_pct=" << cpu_utilization_pct
            << ",peak_rss_kb=" << peak_kb
            << ",delta_peak_rss_kb=" << delta_kb;
#ifdef HE_BENCHMARK_RESOURCE_HEAP
        const auto snapshot = hebench::allocation_snapshot();
        std::cout
            << ",allocations=" << snapshot.allocations
            << ",frees=" << snapshot.frees
            << ",allocated_bytes=" << snapshot.allocated_bytes
            << ",live_bytes=" << snapshot.live_bytes
            << ",peak_live_bytes=" << snapshot.peak_live_bytes;
#endif
        if (!extra.empty())
        {
            std::cout << ',' << extra;
        }
        std::cout << '\n';
    }

#if defined(HE_BENCHMARK_RESOURCE_CORPUS_MEMORY) || defined(HE_BENCHMARK_RESOURCE_THREAD_MEMORY)
    std::vector<int64_t> encode_scalar(std::int64_t value, std::size_t slot_count)
    {
        std::vector<int64_t> values(slot_count, 0);
        values[0] = hebench::centered_mod(value, kPlainModulus);
        return values;
    }

    int requested_threads()
    {
        const char *value = std::getenv("OMP_NUM_THREADS");
        if (!value)
        {
            return 1;
        }
        return std::max(1, std::atoi(value));
    }
#endif
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
        const auto cpu0 = hebench::process_cpu_seconds();
        const hebench::Timer context_timer;
#ifdef HE_BENCHMARK_OPENFHE_RESOURCE_BGV
        lbcrypto::CCParams<lbcrypto::CryptoContextBGVRNS> parameters;
#else
        lbcrypto::CCParams<lbcrypto::CryptoContextBFVRNS> parameters;
#endif
        parameters.SetPlaintextModulus(kPlainModulus);
        parameters.SetMultiplicativeDepth(2);
        parameters.SetSecurityLevel(lbcrypto::HEStd_128_classic);
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size));
        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        crypto_context->Enable(lbcrypto::PKE);
        crypto_context->Enable(lbcrypto::KEYSWITCH);
        crypto_context->Enable(lbcrypto::LEVELEDSHE);
        crypto_context->Enable(lbcrypto::ADVANCEDSHE);
        print_row("resource_context", rows.size(), args.ring_size, true, context_timer.elapsed_ms(), cpu0, baseline_kb);

        lbcrypto::KeyPair<lbcrypto::DCRTPoly> keys;
        {
#ifdef HE_BENCHMARK_RESOURCE_HEAP
            hebench::ScopedAllocationTracking tracker;
#endif
            const auto cpu_before = hebench::process_cpu_seconds();
            const hebench::Timer timer;
            keys = crypto_context->KeyGen();
            crypto_context->EvalMultKeyGen(keys.secretKey);
            crypto_context->EvalRotateKeyGen(keys.secretKey, {1});
            print_row("resource_keygen", rows.size(), args.ring_size, true, timer.elapsed_ms(), cpu_before, baseline_kb);
        }

        lbcrypto::Plaintext plain_a;
        lbcrypto::Plaintext plain_b;
        {
#ifdef HE_BENCHMARK_RESOURCE_HEAP
            hebench::ScopedAllocationTracking tracker;
#endif
            const auto cpu_before = hebench::process_cpu_seconds();
            const hebench::Timer timer;
            plain_a = crypto_context->MakePackedPlaintext(encode_values(rows, false, args.ring_size));
            plain_b = crypto_context->MakePackedPlaintext(encode_values(rows, true, args.ring_size));
            print_row("resource_encode", rows.size(), args.ring_size, true, timer.elapsed_ms(), cpu_before, baseline_kb);
        }

        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> encrypted_a;
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> encrypted_b;
        {
#ifdef HE_BENCHMARK_RESOURCE_HEAP
            hebench::ScopedAllocationTracking tracker;
#endif
            const auto cpu_before = hebench::process_cpu_seconds();
            const hebench::Timer timer;
            encrypted_a = crypto_context->Encrypt(keys.publicKey, plain_a);
            encrypted_b = crypto_context->Encrypt(keys.publicKey, plain_b);
            print_row("resource_encrypt", rows.size(), args.ring_size, true, timer.elapsed_ms(), cpu_before, baseline_kb, "ciphertexts=2");
        }

#if defined(HE_BENCHMARK_RESOURCE_FOOTPRINT)
        print_row("footprint_context", rows.size(), args.ring_size, true, 0.0, hebench::process_cpu_seconds(), baseline_kb);
        print_row("footprint_secret_key", rows.size(), args.ring_size, true, 0.0, hebench::process_cpu_seconds(), baseline_kb);
        print_row("footprint_public_key", rows.size(), args.ring_size, true, 0.0, hebench::process_cpu_seconds(), baseline_kb);
        print_row("footprint_ciphertexts", rows.size(), args.ring_size, true, 0.0, hebench::process_cpu_seconds(), baseline_kb, "ciphertexts=2");
        return 0;
#elif defined(HE_BENCHMARK_RESOURCE_CORPUS_MEMORY) || defined(HE_BENCHMARK_RESOURCE_THREAD_MEMORY)
        std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>> held_ciphertexts;
        held_ciphertexts.reserve(rows.size());
        const auto cpu_before = hebench::process_cpu_seconds();
        const hebench::Timer timer;
        for (const auto &row : rows)
        {
            const auto scalar_plain = crypto_context->MakePackedPlaintext(encode_scalar(row.a, args.ring_size));
            held_ciphertexts.push_back(crypto_context->Encrypt(keys.publicKey, scalar_plain));
        }
        print_row(
            "resource_hold_ciphertexts",
            rows.size(),
            args.ring_size,
            held_ciphertexts.size() == rows.size(),
            timer.elapsed_ms(),
            cpu_before,
            baseline_kb,
            "held_ciphertexts=" + std::to_string(held_ciphertexts.size()) +
                ",requested_threads=" + std::to_string(requested_threads()));
        return 0;
#else
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> product;
        {
#ifdef HE_BENCHMARK_RESOURCE_HEAP
            hebench::ScopedAllocationTracking tracker;
#endif
            const auto cpu_before = hebench::process_cpu_seconds();
            const hebench::Timer timer;
            product = crypto_context->EvalMult(encrypted_a, encrypted_b);
            print_row("resource_multiply", rows.size(), args.ring_size, product != nullptr, timer.elapsed_ms(), cpu_before, baseline_kb);
        }
        {
#ifdef HE_BENCHMARK_RESOURCE_HEAP
            hebench::ScopedAllocationTracking tracker;
#endif
            const auto cpu_before = hebench::process_cpu_seconds();
            const hebench::Timer timer;
            lbcrypto::Plaintext decrypted;
            crypto_context->Decrypt(keys.secretKey, encrypted_a, &decrypted);
            print_row("resource_decrypt", rows.size(), args.ring_size, decrypted != nullptr, timer.elapsed_ms(), cpu_before, baseline_kb);
        }
        return 0;
#endif
    }
    catch (const std::exception &error)
    {
        std::cerr << "openfhe_resource_" << kModeName << " failed: " << error.what() << '\n';
        return 2;
    }
}
