#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hebench
{
    // One row from exact/*.csv. Expected values are stored as raw signed
    // integers; library-specific harnesses normalize them modulo plaintext t.
    struct ExactRow
    {
        std::size_t index = 0;
        std::int64_t a = 0;
        std::int64_t b = 0;
        std::int64_t expected_add = 0;
        std::int64_t expected_sub = 0;
        std::int64_t expected_mul = 0;
        std::int64_t expected_square_a = 0;
        std::int64_t expected_negate_a = 0;
    };

    struct RotationRow
    {
        std::size_t index = 0;
        std::int64_t value = 0;
    };

    inline std::string trim_ascii_whitespace(const std::string &value)
    {
        // The generated CSVs do not quote fields, but trimming keeps this reader
        // tolerant of hand-edited rows used while debugging.
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return "";
        }

        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    inline std::vector<std::string> split_csv_line(const std::string &line)
    {
        // Intentionally simple: the corpus generator only emits numeric,
        // comma-separated fields with no quoting or embedded commas.
        std::vector<std::string> fields;
        std::stringstream stream(line);
        std::string field;

        while (std::getline(stream, field, ','))
        {
            fields.push_back(trim_ascii_whitespace(field));
        }

        return fields;
    }

    inline std::int64_t parse_i64(const std::string &value, const std::string &path, std::size_t line_number)
    {
        // Reject partially parsed values so malformed corpus rows fail close to
        // the source file and line number.
        try
        {
            std::size_t consumed = 0;
            const auto parsed = std::stoll(value, &consumed, 10);
            if (consumed != value.size())
            {
                throw std::invalid_argument("trailing data");
            }
            return parsed;
        }
        catch (const std::exception &)
        {
            throw std::runtime_error(path + ":" + std::to_string(line_number) + ": invalid integer: " + value);
        }
    }

    inline std::vector<ExactRow> read_exact_csv(const std::string &path)
    {
        // Exact CSV schema:
        // index,a,b,expected_add,expected_sub,expected_mul,expected_square_a,expected_negate_a
        std::ifstream file(path);
        if (!file)
        {
            throw std::runtime_error("failed to open CSV: " + path);
        }

        std::string line;
        // Header is required for human inspection and manifest stability, but
        // the benchmark only needs the fixed column order below.
        if (!std::getline(file, line))
        {
            throw std::runtime_error("empty CSV: " + path);
        }

        std::vector<ExactRow> rows;
        std::size_t line_number = 1;

        while (std::getline(file, line))
        {
            ++line_number;
            if (line.empty())
            {
                continue;
            }

            const auto fields = split_csv_line(line);
            if (fields.size() != 8)
            {
                throw std::runtime_error(
                    path + ":" + std::to_string(line_number) + ": expected 8 columns, got " +
                    std::to_string(fields.size()));
            }

            ExactRow row;
            row.index = static_cast<std::size_t>(parse_i64(fields[0], path, line_number));
            row.a = parse_i64(fields[1], path, line_number);
            row.b = parse_i64(fields[2], path, line_number);
            row.expected_add = parse_i64(fields[3], path, line_number);
            row.expected_sub = parse_i64(fields[4], path, line_number);
            row.expected_mul = parse_i64(fields[5], path, line_number);
            row.expected_square_a = parse_i64(fields[6], path, line_number);
            row.expected_negate_a = parse_i64(fields[7], path, line_number);
            rows.push_back(row);
        }

        return rows;
    }

    inline std::vector<RotationRow> read_rotation_csv(const std::string &path)
    {
        // Rotation CSV schema: index,value
        std::ifstream file(path);
        if (!file)
        {
            throw std::runtime_error("failed to open CSV: " + path);
        }

        std::string line;
        if (!std::getline(file, line))
        {
            throw std::runtime_error("empty CSV: " + path);
        }

        std::vector<RotationRow> rows;
        std::size_t line_number = 1;

        while (std::getline(file, line))
        {
            ++line_number;
            if (line.empty())
            {
                continue;
            }

            const auto fields = split_csv_line(line);
            if (fields.size() != 2)
            {
                throw std::runtime_error(
                    path + ":" + std::to_string(line_number) + ": expected 2 columns, got " +
                    std::to_string(fields.size()));
            }

            RotationRow row;
            row.index = static_cast<std::size_t>(parse_i64(fields[0], path, line_number));
            row.value = parse_i64(fields[1], path, line_number);
            rows.push_back(row);
        }

        return rows;
    }
}
