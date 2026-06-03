#pragma once

#include <chrono>

namespace hebench
{
    // Small steady-clock timer for benchmark regions. Construct it immediately
    // before the operation being measured to avoid including setup or checks.
    class Timer
    {
    public:
        Timer() : start_(std::chrono::steady_clock::now()) {}

        double elapsed_ms() const
        {
            const auto end = std::chrono::steady_clock::now();
            return std::chrono::duration<double, std::milli>(end - start_).count();
        }

    private:
        std::chrono::steady_clock::time_point start_;
    };
}
