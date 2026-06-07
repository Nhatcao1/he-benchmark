#pragma once

#include <cstddef>
#include <sys/resource.h>

namespace hebench
{
    inline double process_cpu_seconds()
    {
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) != 0)
        {
            return 0.0;
        }

        const auto user_seconds = static_cast<double>(usage.ru_utime.tv_sec) +
            static_cast<double>(usage.ru_utime.tv_usec) / 1000000.0;
        const auto system_seconds = static_cast<double>(usage.ru_stime.tv_sec) +
            static_cast<double>(usage.ru_stime.tv_usec) / 1000000.0;
        return user_seconds + system_seconds;
    }

    inline std::size_t peak_rss_kb()
    {
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) != 0)
        {
            return 0;
        }

#ifdef __APPLE__
        return static_cast<std::size_t>(usage.ru_maxrss / 1024);
#else
        return static_cast<std::size_t>(usage.ru_maxrss);
#endif
    }
}
