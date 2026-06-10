#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

#include "openfhe.h"

#include "benchmark_args.hpp"
#include "timer.hpp"

using namespace lbcrypto;

namespace
{
#ifdef HE_BENCHMARK_OPENFHE_LOWLEVEL_POLY
    constexpr const char *kBinaryName = "openfhe_lowlevel_poly";
#else
    constexpr const char *kBinaryName = "openfhe_lowlevel_ntt";
#endif

    void print_row(
        const std::string &operation,
        std::size_t ring_size,
        bool correct,
        double elapsed_ms,
        const std::string &extra = "")
    {
        const auto ops_per_sec = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        const auto values_per_sec = elapsed_ms > 0.0 ? static_cast<double>(ring_size) * 1000.0 / elapsed_ms : 0.0;

        std::cout
            << "library=OpenFHE"
            << ",scheme=LOWLEVEL"
            << ",operation=" << operation
            << ",size=" << ring_size
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ",latency_ms=" << elapsed_ms
            << ",ops_per_sec=" << ops_per_sec
            << ",values_per_sec=" << values_per_sec;
        if (!extra.empty())
        {
            std::cout << ',' << extra;
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

        lbcrypto::CCParams<lbcrypto::CryptoContextBFVRNS> parameters;
        parameters.SetPlaintextModulus(786433);
        parameters.SetMultiplicativeDepth(2);
        parameters.SetSecurityLevel(lbcrypto::HEStd_128_classic);
        parameters.SetRingDim(static_cast<std::uint32_t>(args.ring_size));
        parameters.SetBatchSize(static_cast<std::uint32_t>(args.ring_size));
        auto crypto_context = lbcrypto::GenCryptoContext(parameters);
        const auto element_params = crypto_context->GetElementParams();

        lbcrypto::DCRTPoly::DugType dug;
        lbcrypto::DCRTPoly poly_a(dug, element_params, COEFFICIENT);
        lbcrypto::DCRTPoly poly_b(dug, element_params, COEFFICIENT);

#ifdef HE_BENCHMARK_OPENFHE_LOWLEVEL_POLY
        {
            const hebench::Timer timer;
            const auto result = poly_a + poly_b;
            const auto elapsed_ms = timer.elapsed_ms();
            print_row("poly_add", args.ring_size, result.GetLength() == poly_a.GetLength(), elapsed_ms);
        }
        {
            poly_a.SetFormat(EVALUATION);
            poly_b.SetFormat(EVALUATION);
            const hebench::Timer timer;
            const auto result = poly_a * poly_b;
            const auto elapsed_ms = timer.elapsed_ms();
            print_row("poly_dcrt_mul_eval", args.ring_size, result.GetLength() == poly_a.GetLength(), elapsed_ms);
        }
#else
        const auto original = poly_a;
        {
            const hebench::Timer timer;
            poly_a.SetFormat(EVALUATION);
            const auto elapsed_ms = timer.elapsed_ms();
            print_row("ntt_forward", args.ring_size, true, elapsed_ms);
        }
        {
            const hebench::Timer timer;
            poly_a.SetFormat(COEFFICIENT);
            const auto elapsed_ms = timer.elapsed_ms();
            print_row("intt_inverse", args.ring_size, poly_a == original, elapsed_ms);
        }
#endif
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << kBinaryName << " failed: " << error.what() << '\n';
        return 2;
    }
}
