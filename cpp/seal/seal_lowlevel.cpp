#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "seal/seal.h"
#include "seal/util/ntt.h"
#include "seal/util/polyarithsmallmod.h"

#include "benchmark_args.hpp"
#include "timer.hpp"

namespace
{
#ifdef HE_BENCHMARK_SEAL_LOWLEVEL_POLY
    constexpr const char *kBinaryName = "seal_lowlevel_poly";
#else
    constexpr const char *kBinaryName = "seal_lowlevel_ntt";
#endif

    int log2_exact(std::size_t value)
    {
        int result = 0;
        while (value > 1)
        {
            if ((value & 1U) != 0U)
            {
                throw std::runtime_error("ring size must be a power of two");
            }
            value >>= 1U;
            ++result;
        }
        return result;
    }

    std::vector<std::uint64_t> make_poly(std::size_t coeff_count, const seal::Modulus &modulus, std::uint64_t seed)
    {
        std::vector<std::uint64_t> values(coeff_count);
        std::uint64_t state = seed;
        for (auto &value : values)
        {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            value = state % modulus.value();
        }
        return values;
    }

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
            << "library=SEAL"
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

        seal::EncryptionParameters parms(seal::scheme_type::bfv);
        parms.set_poly_modulus_degree(args.ring_size);
        parms.set_coeff_modulus(seal::CoeffModulus::BFVDefault(args.ring_size));
        parms.set_plain_modulus(seal::PlainModulus::Batching(args.ring_size, 20));
        seal::SEALContext context(parms);
        if (!context.parameters_set())
        {
            throw std::runtime_error(context.parameter_error_message());
        }

        const auto context_data = context.key_context_data();
        const auto &modulus = parms.coeff_modulus().front();
        const auto *ntt_tables = context_data->small_ntt_tables();
        auto operand_a = make_poly(args.ring_size, modulus, 17);
        auto operand_b = make_poly(args.ring_size, modulus, 29);
        std::vector<std::uint64_t> result(args.ring_size);

#ifdef HE_BENCHMARK_SEAL_LOWLEVEL_POLY
        {
            const hebench::Timer timer;
            seal::util::add_poly_coeffmod(operand_a.data(), operand_b.data(), args.ring_size, modulus, result.data());
            const auto elapsed_ms = timer.elapsed_ms();
            bool correct = true;
            for (std::size_t i = 0; i < result.size(); ++i)
            {
                const auto expected = (operand_a[i] + operand_b[i]) % modulus.value();
                if (result[i] != expected)
                {
                    correct = false;
                    break;
                }
            }
            print_row("poly_add_coeffmod", args.ring_size, correct, elapsed_ms, "modulus_bits=" + std::to_string(modulus.bit_count()));
        }
        {
            const hebench::Timer timer;
            seal::util::dyadic_product_coeffmod(
                operand_a.data(), operand_b.data(), args.ring_size, modulus, result.data());
            const auto elapsed_ms = timer.elapsed_ms();
            bool correct = true;
            for (std::size_t i = 0; i < result.size(); ++i)
            {
                const auto expected = seal::util::multiply_uint_mod(operand_a[i], operand_b[i], modulus);
                if (result[i] != expected)
                {
                    correct = false;
                    break;
                }
            }
            print_row("poly_dyadic_mul_coeffmod", args.ring_size, correct, elapsed_ms, "modulus_bits=" + std::to_string(modulus.bit_count()));
        }
        {
            const hebench::Timer timer;
            seal::util::negacyclic_shift_poly_coeffmod(operand_a.data(), args.ring_size, 1, modulus, result.data());
            const auto elapsed_ms = timer.elapsed_ms();
            print_row("poly_negacyclic_shift", args.ring_size, true, elapsed_ms, "shift=1,modulus_bits=" + std::to_string(modulus.bit_count()));
        }
#else
        const auto original = operand_a;
        {
            const hebench::Timer timer;
            seal::util::ntt_negacyclic_harvey(operand_a.data(), ntt_tables[0]);
            const auto elapsed_ms = timer.elapsed_ms();
            print_row(
                "ntt_forward",
                args.ring_size,
                true,
                elapsed_ms,
                "coeff_count_power=" + std::to_string(log2_exact(args.ring_size)) +
                    ",modulus_bits=" + std::to_string(modulus.bit_count()));
        }
        {
            const hebench::Timer timer;
            seal::util::inverse_ntt_negacyclic_harvey(operand_a.data(), ntt_tables[0]);
            const auto elapsed_ms = timer.elapsed_ms();
            print_row(
                "intt_inverse",
                args.ring_size,
                operand_a == original,
                elapsed_ms,
                "coeff_count_power=" + std::to_string(log2_exact(args.ring_size)) +
                    ",modulus_bits=" + std::to_string(modulus.bit_count()));
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
