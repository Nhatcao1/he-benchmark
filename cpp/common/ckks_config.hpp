#pragma once

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "benchmark_args.hpp"

namespace hebench
{
    struct CkksBenchmarkConfig
    {
        std::string name;
        std::size_t multiplicative_depth;
        std::size_t scale_bits;
        std::size_t first_mod_bits;
        bool explicit_first_mod;
        bool relaxed_security;
    };

    inline CkksBenchmarkConfig ckks_config_for(
        const BenchmarkArgs &args,
        std::size_t default_depth,
        std::size_t default_scale_bits,
        std::size_t default_first_mod_bits = 60)
    {
        CkksBenchmarkConfig config{};
        config.name = args.ckks_config;

        if (args.ckks_config == "default")
        {
            config.multiplicative_depth = args.ckks_depth > 0 ? args.ckks_depth : default_depth;
            config.scale_bits = args.ckks_scale_bits > 0 ? args.ckks_scale_bits : default_scale_bits;
            config.first_mod_bits = args.ckks_first_mod_bits > 0 ? args.ckks_first_mod_bits : default_first_mod_bits;
            config.explicit_first_mod = args.ckks_first_mod_bits > 0;
            config.relaxed_security = false;
        }
        else if (args.ckks_config == "ring-sweep")
        {
            config.multiplicative_depth = args.ckks_depth > 0 ? args.ckks_depth : 1;
            config.scale_bits = args.ckks_scale_bits > 0 ? args.ckks_scale_bits : 30;
            config.first_mod_bits = args.ckks_first_mod_bits > 0 ? args.ckks_first_mod_bits : 49;
            config.explicit_first_mod = true;
            config.relaxed_security = true;
        }
        else
        {
            throw std::runtime_error("unknown --ckks-config: " + args.ckks_config);
        }

        if (config.multiplicative_depth == 0)
        {
            throw std::runtime_error("CKKS multiplicative depth must be positive");
        }
        if (config.scale_bits == 0)
        {
            throw std::runtime_error("CKKS scale bits must be positive");
        }
        if (config.first_mod_bits == 0)
        {
            throw std::runtime_error("CKKS first modulus bits must be positive");
        }
        if (config.first_mod_bits < config.scale_bits)
        {
            throw std::runtime_error("CKKS first modulus bits must be >= scale bits");
        }

        return config;
    }

    inline double ckks_scale(const CkksBenchmarkConfig &config)
    {
        return std::pow(2.0, static_cast<double>(config.scale_bits));
    }

    inline std::vector<int> seal_ckks_coeff_modulus_bits(const CkksBenchmarkConfig &config)
    {
        std::vector<int> bits;
        bits.reserve(config.multiplicative_depth + 2);
        bits.push_back(static_cast<int>(config.first_mod_bits));
        for (std::size_t i = 0; i < config.multiplicative_depth; ++i)
        {
            bits.push_back(static_cast<int>(config.scale_bits));
        }
        bits.push_back(static_cast<int>(config.first_mod_bits));
        return bits;
    }

    inline std::string ckks_config_extra(const CkksBenchmarkConfig &config)
    {
        return "ckks_config=" + config.name +
            ",ckks_depth=" + std::to_string(config.multiplicative_depth) +
            ",scale_bits=" + std::to_string(config.scale_bits) +
            ",first_mod_bits=" + std::to_string(config.first_mod_bits) +
            ",security=" + std::string(config.relaxed_security ? "not_set" : "default");
    }
}
