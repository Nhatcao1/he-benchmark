#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>

namespace hebench
{
    struct ThroughputResult
    {
        std::uint64_t completed = 0;
        double elapsed_seconds = 0.0;
    };

    template <typename Operation>
    ThroughputResult run_for_duration(std::size_t duration_ms, Operation &&operation)
    {
        const auto start = std::chrono::steady_clock::now();
        const auto deadline = start + std::chrono::milliseconds(duration_ms);
        std::uint64_t completed = 0;

        // Keep correctness checks outside this loop. This benchmark measures
        // sustained operation throughput, so validation is done on sampled
        // outputs after timing has stopped.
        do
        {
            operation();
            ++completed;
        } while (std::chrono::steady_clock::now() < deadline);

        const auto stop = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration<double>(stop - start).count();
        return {completed, elapsed};
    }

    inline double safe_rate(std::uint64_t count, double seconds)
    {
        return seconds > 0.0 ? static_cast<double>(count) / seconds : 0.0;
    }

    inline double safe_rate(double count, double seconds)
    {
        return seconds > 0.0 ? count / seconds : 0.0;
    }

    inline std::string throughput_base_extra(
        const ThroughputResult &result,
        std::size_t requested_duration_ms,
        std::size_t active_slots,
        std::size_t slot_capacity)
    {
        const auto operations_per_sec = safe_rate(result.completed, result.elapsed_seconds);
        const auto slot_utilization = slot_capacity > 0
            ? static_cast<double>(active_slots) / static_cast<double>(slot_capacity)
            : 0.0;

        std::ostringstream stream;
        stream
            << "duration_ms=" << requested_duration_ms
            << ",elapsed_seconds=" << result.elapsed_seconds
            << ",active_slots=" << active_slots
            << ",slot_capacity=" << slot_capacity
            << ",slot_utilization=" << slot_utilization
            << ",completed_operations=" << result.completed
            << ",operations_per_sec=" << operations_per_sec
            << ",packed_values_per_sec=" << operations_per_sec * static_cast<double>(active_slots);
        return stream.str();
    }

    inline void print_throughput_row(
        const std::string &library,
        const std::string &scheme,
        const std::string &operation,
        std::size_t size,
        std::size_t ring_size,
        bool correct,
        const std::string &extra,
        const std::string &error = "")
    {
        std::cout
            << "library=" << library
            << ",scheme=" << scheme
            << ",operation=" << operation
            << ",size=" << size
            << ",ring_size=" << ring_size
            << ",correct=" << (correct ? "true" : "false")
            << ',' << extra;
        if (!correct && !error.empty())
        {
            std::cout << ",error=\"" << error << "\"";
        }
        std::cout << '\n';
    }
}
