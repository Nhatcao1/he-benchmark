#pragma once

#include <atomic>
#include <cstdlib>
#include <new>

namespace hebench
{
    struct AllocationSnapshot
    {
        std::size_t allocations = 0;
        std::size_t frees = 0;
        std::size_t allocated_bytes = 0;
        std::size_t live_bytes = 0;
        std::size_t peak_live_bytes = 0;
    };

    inline std::atomic<bool> &allocation_tracking_enabled()
    {
        static std::atomic<bool> enabled{false};
        return enabled;
    }

    inline std::atomic<std::size_t> &allocation_count()
    {
        static std::atomic<std::size_t> value{0};
        return value;
    }

    inline std::atomic<std::size_t> &free_count()
    {
        static std::atomic<std::size_t> value{0};
        return value;
    }

    inline std::atomic<std::size_t> &allocated_bytes()
    {
        static std::atomic<std::size_t> value{0};
        return value;
    }

    inline std::atomic<std::size_t> &live_bytes()
    {
        static std::atomic<std::size_t> value{0};
        return value;
    }

    inline std::atomic<std::size_t> &peak_live_bytes()
    {
        static std::atomic<std::size_t> value{0};
        return value;
    }

    inline void reset_allocation_tracking()
    {
        allocation_count().store(0, std::memory_order_relaxed);
        free_count().store(0, std::memory_order_relaxed);
        allocated_bytes().store(0, std::memory_order_relaxed);
        live_bytes().store(0, std::memory_order_relaxed);
        peak_live_bytes().store(0, std::memory_order_relaxed);
    }

    inline void set_allocation_tracking(bool enabled)
    {
        allocation_tracking_enabled().store(enabled, std::memory_order_release);
    }

    inline void record_allocation(std::size_t size)
    {
        allocation_count().fetch_add(1, std::memory_order_relaxed);
        allocated_bytes().fetch_add(size, std::memory_order_relaxed);
        const auto live = live_bytes().fetch_add(size, std::memory_order_relaxed) + size;
        auto peak = peak_live_bytes().load(std::memory_order_relaxed);
        while (live > peak && !peak_live_bytes().compare_exchange_weak(
                                  peak, live, std::memory_order_relaxed, std::memory_order_relaxed))
        {
        }
    }

    inline void record_free(std::size_t size)
    {
        free_count().fetch_add(1, std::memory_order_relaxed);
        live_bytes().fetch_sub(size, std::memory_order_relaxed);
    }

    inline AllocationSnapshot allocation_snapshot()
    {
        return {
            allocation_count().load(std::memory_order_relaxed),
            free_count().load(std::memory_order_relaxed),
            allocated_bytes().load(std::memory_order_relaxed),
            live_bytes().load(std::memory_order_relaxed),
            peak_live_bytes().load(std::memory_order_relaxed),
        };
    }

    class ScopedAllocationTracking
    {
    public:
        ScopedAllocationTracking()
        {
            reset_allocation_tracking();
            set_allocation_tracking(true);
        }

        ~ScopedAllocationTracking()
        {
            set_allocation_tracking(false);
        }
    };
}

void *operator new(std::size_t size)
{
    void *ptr = std::malloc(size == 0 ? 1 : size);
    if (!ptr)
    {
        throw std::bad_alloc();
    }

    if (hebench::allocation_tracking_enabled().load(std::memory_order_acquire))
    {
        hebench::record_allocation(size);
    }
    return ptr;
}

void *operator new[](std::size_t size)
{
    return operator new(size);
}

void operator delete(void *ptr) noexcept
{
    std::free(ptr);
}

void operator delete[](void *ptr) noexcept
{
    std::free(ptr);
}

void operator delete(void *ptr, std::size_t size) noexcept
{
    if (ptr && hebench::allocation_tracking_enabled().load(std::memory_order_acquire))
    {
        hebench::record_free(size);
    }
    std::free(ptr);
}

void operator delete[](void *ptr, std::size_t size) noexcept
{
    operator delete(ptr, size);
}
