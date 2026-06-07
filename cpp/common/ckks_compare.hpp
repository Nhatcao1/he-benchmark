#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "csv_reader.hpp"

namespace hebench
{
    enum class CkksOperation
    {
        add,
        sub,
        mul,
        add_plain,
        sub_plain,
        mul_plain,
        square_a,
        negate_a,
        rescale,
    };

    struct CkksMetrics
    {
        bool correct = false;
        std::size_t count = 0;
        std::size_t passed = 0;
        double pass_rate = 0.0;
        double mae = 0.0;
        double rmse = 0.0;
        double max_abs_error = 0.0;
        double mean_relative_error = 0.0;
        double max_relative_error = 0.0;
        double precision_bits = 0.0;
        std::string error;
    };

    inline double expected_for(const CkksRow &row, CkksOperation operation)
    {
        switch (operation)
        {
        case CkksOperation::add:
        case CkksOperation::add_plain:
            return row.expected_add;
        case CkksOperation::sub:
        case CkksOperation::sub_plain:
            return row.expected_sub;
        case CkksOperation::mul:
        case CkksOperation::mul_plain:
        case CkksOperation::rescale:
            return row.expected_mul;
        case CkksOperation::square_a:
            return row.expected_square_a;
        case CkksOperation::negate_a:
            return row.expected_negate_a;
        }

        return 0.0;
    }

    inline std::string operation_name(CkksOperation operation)
    {
        switch (operation)
        {
        case CkksOperation::add:
            return "add";
        case CkksOperation::sub:
            return "sub";
        case CkksOperation::mul:
            return "mul";
        case CkksOperation::add_plain:
            return "add_plain";
        case CkksOperation::sub_plain:
            return "sub_plain";
        case CkksOperation::mul_plain:
            return "mul_plain";
        case CkksOperation::square_a:
            return "square_a";
        case CkksOperation::negate_a:
            return "negate_a";
        case CkksOperation::rescale:
            return "rescale";
        }

        return "unknown";
    }

    inline CkksMetrics compare_ckks_slots(
        const std::vector<double> &actual,
        const std::vector<CkksRow> &rows,
        CkksOperation operation,
        double abs_tolerance = 1e-3,
        double rel_tolerance = 1e-3,
        double pass_rate_threshold = 1.0,
        double relative_epsilon = 1e-12)
    {
        CkksMetrics metrics;
        metrics.count = rows.size();

        if (actual.size() < rows.size())
        {
            metrics.error = "decoded slot count is smaller than corpus row count";
            return metrics;
        }
        if (rows.empty())
        {
            metrics.correct = true;
            return metrics;
        }

        double sum_abs = 0.0;
        double sum_sq = 0.0;
        double sum_rel = 0.0;
        std::size_t first_failed = std::numeric_limits<std::size_t>::max();
        double first_expected = 0.0;
        double first_actual = 0.0;
        double first_abs = 0.0;
        double first_rel = 0.0;

        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto expected = expected_for(rows[i], operation);
            const auto error = std::abs(actual[i] - expected);
            const auto denom = std::max(std::abs(expected), relative_epsilon);
            const auto rel = error / denom;

            sum_abs += error;
            sum_sq += error * error;
            sum_rel += rel;
            metrics.max_abs_error = std::max(metrics.max_abs_error, error);
            metrics.max_relative_error = std::max(metrics.max_relative_error, rel);

            if (error <= abs_tolerance || rel <= rel_tolerance)
            {
                ++metrics.passed;
            }
            else if (first_failed == std::numeric_limits<std::size_t>::max())
            {
                first_failed = i;
                first_expected = expected;
                first_actual = actual[i];
                first_abs = error;
                first_rel = rel;
            }
        }

        const auto n = static_cast<double>(rows.size());
        metrics.pass_rate = static_cast<double>(metrics.passed) / n;
        metrics.mae = sum_abs / n;
        metrics.rmse = std::sqrt(sum_sq / n);
        metrics.mean_relative_error = sum_rel / n;
        metrics.precision_bits = metrics.mean_relative_error > 0.0
            ? -std::log2(metrics.mean_relative_error)
            : std::numeric_limits<double>::infinity();
        metrics.correct = metrics.pass_rate >= pass_rate_threshold;

        if (!metrics.correct && first_failed != std::numeric_limits<std::size_t>::max())
        {
            metrics.error = "slot " + std::to_string(first_failed) +
                " expected " + std::to_string(first_expected) +
                " got " + std::to_string(first_actual) +
                " abs_error " + std::to_string(first_abs) +
                " rel_error " + std::to_string(first_rel);
        }

        return metrics;
    }

    inline CkksMetrics compare_ckks_vectors(
        const std::vector<double> &actual,
        const std::vector<double> &expected,
        double abs_tolerance = 1e-3,
        double rel_tolerance = 1e-3,
        double pass_rate_threshold = 1.0,
        double relative_epsilon = 1e-12)
    {
        CkksMetrics metrics;
        metrics.count = expected.size();

        if (actual.size() < expected.size())
        {
            metrics.error = "decoded slot count is smaller than expected value count";
            return metrics;
        }
        if (expected.empty())
        {
            metrics.correct = true;
            return metrics;
        }

        double sum_abs = 0.0;
        double sum_sq = 0.0;
        double sum_rel = 0.0;
        std::size_t first_failed = std::numeric_limits<std::size_t>::max();

        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            const auto error = std::abs(actual[i] - expected[i]);
            const auto denom = std::max(std::abs(expected[i]), relative_epsilon);
            const auto rel = error / denom;

            sum_abs += error;
            sum_sq += error * error;
            sum_rel += rel;
            metrics.max_abs_error = std::max(metrics.max_abs_error, error);
            metrics.max_relative_error = std::max(metrics.max_relative_error, rel);

            if (error <= abs_tolerance || rel <= rel_tolerance)
            {
                ++metrics.passed;
            }
            else if (first_failed == std::numeric_limits<std::size_t>::max())
            {
                first_failed = i;
                metrics.error = "slot " + std::to_string(i) +
                    " expected " + std::to_string(expected[i]) +
                    " got " + std::to_string(actual[i]) +
                    " abs_error " + std::to_string(error) +
                    " rel_error " + std::to_string(rel);
            }
        }

        const auto n = static_cast<double>(expected.size());
        metrics.pass_rate = static_cast<double>(metrics.passed) / n;
        metrics.mae = sum_abs / n;
        metrics.rmse = std::sqrt(sum_sq / n);
        metrics.mean_relative_error = sum_rel / n;
        metrics.precision_bits = metrics.mean_relative_error > 0.0
            ? -std::log2(metrics.mean_relative_error)
            : std::numeric_limits<double>::infinity();
        metrics.correct = metrics.pass_rate >= pass_rate_threshold;

        return metrics;
    }
}
