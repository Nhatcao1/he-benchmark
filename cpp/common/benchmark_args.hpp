#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace hebench
{
    struct BenchmarkArgs
    {
        std::string corpus_path = "he_corpus/exact/exact_safe_000008.csv";
        std::size_t ring_size = 8192;
        std::size_t max_depth = 4;
        bool show_help = false;
    };

    inline std::size_t parse_size_arg(const std::string &value, const std::string &option_name)
    {
        try
        {
            std::size_t consumed = 0;
            const auto parsed = std::stoull(value, &consumed, 10);
            if (consumed != value.size() || parsed == 0)
            {
                throw std::invalid_argument("invalid size");
            }
            return static_cast<std::size_t>(parsed);
        }
        catch (const std::exception &)
        {
            throw std::runtime_error("invalid value for " + option_name + ": " + value);
        }
    }

    inline BenchmarkArgs parse_benchmark_args(int argc, char **argv)
    {
        BenchmarkArgs args;

        for (int i = 1; i < argc; ++i)
        {
            const std::string option = argv[i];

            if (option == "--help" || option == "-h")
            {
                args.show_help = true;
                return args;
            }
            if (option == "--corpus")
            {
                if (++i >= argc)
                {
                    throw std::runtime_error("missing value for --corpus");
                }
                args.corpus_path = argv[i];
                continue;
            }
            if (option == "--ring-size")
            {
                if (++i >= argc)
                {
                    throw std::runtime_error("missing value for --ring-size");
                }
                args.ring_size = parse_size_arg(argv[i], "--ring-size");
                continue;
            }
            if (option == "--max-depth")
            {
                if (++i >= argc)
                {
                    throw std::runtime_error("missing value for --max-depth");
                }
                args.max_depth = parse_size_arg(argv[i], "--max-depth");
                continue;
            }
            if (!option.empty() && option[0] == '-')
            {
                throw std::runtime_error("unknown option: " + option);
            }

            // Backward-compatible positional corpus path for older notes/scripts.
            args.corpus_path = option;
        }

        return args;
    }

    inline std::string benchmark_usage(const std::string &program_name)
    {
        return "Usage: " + program_name +
            " [--corpus he_corpus/exact/exact_safe_000008.csv] [--ring-size 8192] [--max-depth 4]\n";
    }
}
