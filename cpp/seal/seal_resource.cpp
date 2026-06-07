#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef HE_BENCHMARK_RESOURCE_HEAP
#include "allocation_tracker.hpp"
#endif
#include "seal/seal.h"

#include "benchmark_args.hpp"
#include "csv_reader.hpp"
#include "exact_compare.hpp"
#include "memory_usage.hpp"
#include "timer.hpp"

namespace
{
    constexpr std::uint64_t kPlainModulusBitSize = 20;

#ifdef HE_BENCHMARK_SEAL_RESOURCE_BGV
    constexpr auto kScheme = seal::scheme_type::bgv;
    constexpr const char *kSchemeName = "BGV";
#else
    constexpr auto kScheme = seal::scheme_type::bfv;
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

    template <typename T>
    std::size_t serialized_size(const T &value)
    {
        std::stringstream stream;
        value.save(stream);
        return stream.str().size();
    }

    std::vector<std::uint64_t> encode_scalar(
        std::int64_t value,
        std::size_t slot_count,
        std::uint64_t plain_modulus)
    {
        std::vector<std::uint64_t> values(slot_count, 0ULL);
        const auto centered = hebench::centered_mod(value, plain_modulus);
        values[0] = centered < 0
            ? static_cast<std::uint64_t>(centered + static_cast<std::int64_t>(plain_modulus))
            : static_cast<std::uint64_t>(centered);
        return values;
    }

    std::vector<std::uint64_t> encode_values(
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
            << "library=SEAL"
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

    int requested_threads()
    {
        const char *value = std::getenv("OMP_NUM_THREADS");
        if (!value)
        {
            return 1;
        }
        return std::max(1, std::atoi(value));
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
        const auto cpu0 = hebench::process_cpu_seconds();
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
        print_row("resource_context", rows.size(), args.ring_size, true, context_timer.elapsed_ms(), cpu0, baseline_kb);

        seal::BatchEncoder encoder(context);
        const auto slot_count = encoder.slot_count();
        const auto plain_modulus = parms.plain_modulus().value();
        seal::KeyGenerator keygen(context);

#ifdef HE_BENCHMARK_RESOURCE_FOOTPRINT
        const auto secret_key = keygen.secret_key();
        seal::PublicKey public_key;
        keygen.create_public_key(public_key);
        seal::RelinKeys relin_keys;
        keygen.create_relin_keys(relin_keys);
        seal::GaloisKeys galois_keys;
        keygen.create_galois_keys(std::vector<int>{1}, galois_keys);
        seal::Plaintext plain;
        encoder.encode(encode_values(rows, false, slot_count, plain_modulus), plain);
        seal::Encryptor encryptor(context, public_key);
        seal::Ciphertext encrypted;
        encryptor.encrypt(plain, encrypted);
        print_row("footprint_secret_key", rows.size(), args.ring_size, true, 0.0, hebench::process_cpu_seconds(), baseline_kb, "byte_size=" + std::to_string(serialized_size(secret_key)));
        print_row("footprint_public_key", rows.size(), args.ring_size, true, 0.0, hebench::process_cpu_seconds(), baseline_kb, "byte_size=" + std::to_string(serialized_size(public_key)));
        print_row("footprint_relin_key", rows.size(), args.ring_size, true, 0.0, hebench::process_cpu_seconds(), baseline_kb, "byte_size=" + std::to_string(serialized_size(relin_keys)));
        print_row("footprint_rotation_key", rows.size(), args.ring_size, true, 0.0, hebench::process_cpu_seconds(), baseline_kb, "byte_size=" + std::to_string(serialized_size(galois_keys)));
        print_row("footprint_plaintext", rows.size(), args.ring_size, true, 0.0, hebench::process_cpu_seconds(), baseline_kb, "byte_size=" + std::to_string(serialized_size(plain)));
        print_row("footprint_ciphertext", rows.size(), args.ring_size, true, 0.0, hebench::process_cpu_seconds(), baseline_kb, "byte_size=" + std::to_string(serialized_size(encrypted)));
        return 0;
#else
        seal::PublicKey public_key;
        seal::RelinKeys relin_keys;
        {
#ifdef HE_BENCHMARK_RESOURCE_HEAP
            hebench::ScopedAllocationTracking tracker;
#endif
            const auto cpu_before = hebench::process_cpu_seconds();
            const hebench::Timer timer;
            keygen.create_public_key(public_key);
            keygen.create_relin_keys(relin_keys);
            print_row("resource_keygen", rows.size(), args.ring_size, true, timer.elapsed_ms(), cpu_before, baseline_kb);
        }

        seal::Plaintext plain_a;
        seal::Plaintext plain_b;
        {
#ifdef HE_BENCHMARK_RESOURCE_HEAP
            hebench::ScopedAllocationTracking tracker;
#endif
            const auto cpu_before = hebench::process_cpu_seconds();
            const hebench::Timer timer;
            encoder.encode(encode_values(rows, false, slot_count, plain_modulus), plain_a);
            encoder.encode(encode_values(rows, true, slot_count, plain_modulus), plain_b);
            print_row("resource_encode", rows.size(), args.ring_size, true, timer.elapsed_ms(), cpu_before, baseline_kb);
        }

        seal::Encryptor encryptor(context, public_key);
        seal::Evaluator evaluator(context);
        seal::Decryptor decryptor(context, keygen.secret_key());
        seal::Ciphertext encrypted_a;
        seal::Ciphertext encrypted_b;
        {
#ifdef HE_BENCHMARK_RESOURCE_HEAP
            hebench::ScopedAllocationTracking tracker;
#endif
            const auto cpu_before = hebench::process_cpu_seconds();
            const hebench::Timer timer;
            encryptor.encrypt(plain_a, encrypted_a);
            encryptor.encrypt(plain_b, encrypted_b);
            print_row("resource_encrypt", rows.size(), args.ring_size, true, timer.elapsed_ms(), cpu_before, baseline_kb, "ciphertexts=2");
        }

#if defined(HE_BENCHMARK_RESOURCE_CORPUS_MEMORY) || defined(HE_BENCHMARK_RESOURCE_THREAD_MEMORY)
        std::vector<seal::Ciphertext> held_ciphertexts;
        held_ciphertexts.reserve(rows.size());
        const auto cpu_before = hebench::process_cpu_seconds();
        const hebench::Timer timer;
        for (const auto &row : rows)
        {
            seal::Plaintext scalar_plain;
            encoder.encode(encode_scalar(row.a, slot_count, plain_modulus), scalar_plain);
            held_ciphertexts.emplace_back();
            encryptor.encrypt(scalar_plain, held_ciphertexts.back());
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
        seal::Ciphertext product;
        {
#ifdef HE_BENCHMARK_RESOURCE_HEAP
            hebench::ScopedAllocationTracking tracker;
#endif
            const auto cpu_before = hebench::process_cpu_seconds();
            const hebench::Timer timer;
            evaluator.multiply(encrypted_a, encrypted_b, product);
            evaluator.relinearize_inplace(product, relin_keys);
            print_row("resource_multiply", rows.size(), args.ring_size, true, timer.elapsed_ms(), cpu_before, baseline_kb, "components_after=" + std::to_string(product.size()));
        }
        {
#ifdef HE_BENCHMARK_RESOURCE_HEAP
            hebench::ScopedAllocationTracking tracker;
#endif
            const auto cpu_before = hebench::process_cpu_seconds();
            const hebench::Timer timer;
            seal::Plaintext decrypted;
            decryptor.decrypt(encrypted_a, decrypted);
            print_row("resource_decrypt", rows.size(), args.ring_size, true, timer.elapsed_ms(), cpu_before, baseline_kb);
        }
        return 0;
#endif
#endif
    }
    catch (const std::exception &error)
    {
        std::cerr << "seal_resource_" << kModeName << " failed: " << error.what() << '\n';
        return 2;
    }
}
