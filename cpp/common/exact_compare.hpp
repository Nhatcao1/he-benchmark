#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "csv_reader.hpp"

namespace hebench
{
    // Operations currently covered by exact/*.csv. Keep this enum aligned with
    // ExactRow expected_* columns and operation_name().
    enum class ExactOperation
    {
        add,
        sub,
        mul,
        add_plain,
        sub_plain,
        mul_plain,
        square_a,
        negate_a,
    };

    inline std::int64_t centered_mod(std::int64_t value, std::uint64_t plain_modulus)
    {
        // Exact corpus expectations are signed integers, while BFV/BGV encode
        // values modulo t. Comparing in centered representative form makes
        // negative plaintexts line up across libraries.
        const auto modulus = static_cast<std::int64_t>(plain_modulus);
        auto reduced = value % modulus;
        if (reduced < 0)
        {
            reduced += modulus;
        }
        if (reduced > modulus / 2)
        {
            reduced -= modulus;
        }
        return reduced;
    }

    inline std::int64_t centered_mod_u64(std::uint64_t value, std::uint64_t plain_modulus)
    {
        // Decoders return unsigned residues in [0, t). Convert the same residue
        // into the signed centered interval used by centered_mod().
        const auto reduced = value % plain_modulus;
        if (reduced > plain_modulus / 2)
        {
            return static_cast<std::int64_t>(reduced) - static_cast<std::int64_t>(plain_modulus);
        }
        return static_cast<std::int64_t>(reduced);
    }

    inline std::int64_t expected_for(const ExactRow &row, ExactOperation operation)
    {
        // Centralize column selection so SEAL and OpenFHE runners can share the
        // same correctness path.
        switch (operation)
        {
        case ExactOperation::add:
        case ExactOperation::add_plain:
            return row.expected_add;
        case ExactOperation::sub:
        case ExactOperation::sub_plain:
            return row.expected_sub;
        case ExactOperation::mul:
        case ExactOperation::mul_plain:
            return row.expected_mul;
        case ExactOperation::square_a:
            return row.expected_square_a;
        case ExactOperation::negate_a:
            return row.expected_negate_a;
        }

        return 0;
    }

    inline std::string operation_name(ExactOperation operation)
    {
        switch (operation)
        {
        case ExactOperation::add:
            return "add";
        case ExactOperation::sub:
            return "sub";
        case ExactOperation::mul:
            return "mul";
        case ExactOperation::add_plain:
            return "add_plain";
        case ExactOperation::sub_plain:
            return "sub_plain";
        case ExactOperation::mul_plain:
            return "mul_plain";
        case ExactOperation::square_a:
            return "square_a";
        case ExactOperation::negate_a:
            return "negate_a";
        }

        return "unknown";
    }

    inline bool compare_exact_slots(
        const std::vector<std::uint64_t> &actual,
        const std::vector<ExactRow> &rows,
        ExactOperation operation,
        std::uint64_t plain_modulus,
        std::string &error)
    {
        // Only corpus-sized prefix slots are meaningful. Extra packed slots are
        // zero padding used to fill the encoder's batch size.
        if (actual.size() < rows.size())
        {
            error = "decoded slot count is smaller than corpus row count";
            return false;
        }

        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto expected = centered_mod(expected_for(rows[i], operation), plain_modulus);
            const auto decoded = centered_mod_u64(actual[i], plain_modulus);

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
}
