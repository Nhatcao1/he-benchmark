#include <exception>
#include <iostream>
#include <string>

#include "benchmark_args.hpp"
#include "csv_reader.hpp"

namespace
{
#ifdef HE_BENCHMARK_SEAL_KEYSWITCH_CKKS
    constexpr const char *kSchemeName = "CKKS";
    constexpr const char *kBinaryName = "seal_ckks_keyswitch";
#elif defined(HE_BENCHMARK_SEAL_KEYSWITCH_BGV)
    constexpr const char *kSchemeName = "BGV";
    constexpr const char *kBinaryName = "seal_bgv_keyswitch";
#else
    constexpr const char *kSchemeName = "BFV";
    constexpr const char *kBinaryName = "seal_bfv_keyswitch";
#endif

    void print_unsupported(const std::string &operation, std::size_t size, std::size_t ring_size)
    {
        std::cout
            << "library=SEAL"
            << ",scheme=" << kSchemeName
            << ",operation=" << operation
            << ",size=" << size
            << ",ring_size=" << ring_size
            << ",correct=true"
            << ",latency_ms=0"
            << ",ops_per_sec=0"
            << ",values_per_sec=0"
            << ",supported=false"
            << ",reason=\"SEAL exposes relinearization and Galois-key switching, but not generic public key-to-key switching\""
            << '\n';
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

#ifdef HE_BENCHMARK_SEAL_KEYSWITCH_CKKS
        const auto rows = hebench::read_ckks_csv(args.corpus_path);
#else
        const auto rows = hebench::read_exact_csv(args.corpus_path);
#endif
        if (rows.empty())
        {
            throw std::runtime_error("corpus has no rows: " + args.corpus_path);
        }

        print_unsupported("key_switch_keygen", rows.size(), args.ring_size);
        print_unsupported("key_switch_apply", rows.size(), args.ring_size);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << kBinaryName << " failed: " << error.what() << '\n';
        return 2;
    }
}
