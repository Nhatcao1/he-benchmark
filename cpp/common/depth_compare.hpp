#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "csv_reader.hpp"
#include "exact_compare.hpp"

namespace hebench
{
    inline std::vector<std::int64_t> exact_depth_expected(
        const std::vector<ExactDepthRow> &rows,
        std::size_t depth,
        std::uint64_t plain_modulus)
    {
        std::vector<std::int64_t> expected;
        expected.reserve(rows.size());
        for (const auto &row : rows)
        {
            if (depth == 0 || depth > row.expected_depth.size())
            {
                throw std::runtime_error("requested exact depth is not present in corpus");
            }
            expected.push_back(centered_mod(row.expected_depth[depth - 1], plain_modulus));
        }
        return expected;
    }

    inline bool compare_exact_depth_slots(
        const std::vector<std::uint64_t> &actual,
        const std::vector<std::int64_t> &expected,
        std::uint64_t plain_modulus,
        std::string &error)
    {
        if (actual.size() < expected.size())
        {
            error = "decoded slot count is smaller than expected value count";
            return false;
        }

        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            const auto decoded = centered_mod_u64(actual[i], plain_modulus);
            if (decoded != expected[i])
            {
                error = "slot " + std::to_string(i) + " expected " + std::to_string(expected[i]) +
                    " got " + std::to_string(decoded);
                return false;
            }
        }

        error.clear();
        return true;
    }

    inline std::vector<double> ckks_depth_expected(
        const std::vector<CkksDepthRow> &rows,
        std::size_t depth)
    {
        std::vector<double> expected;
        expected.reserve(rows.size());
        for (const auto &row : rows)
        {
            if (depth == 0 || depth > row.expected_depth.size())
            {
                throw std::runtime_error("requested CKKS depth is not present in corpus");
            }
            expected.push_back(row.expected_depth[depth - 1]);
        }
        return expected;
    }
}
