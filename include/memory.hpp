#pragma once

#include <cstdint>

namespace smoothdemon {

    struct MemoryMetrics {
        std::uint64_t total_kib{};
        std::uint64_t free_kib{};
        std::uint64_t available_kib{};
        std::uint64_t buffers_kib{};
        std::uint64_t cached_kib{};
        std::uint64_t swap_total_kib{};
        std::uint64_t swap_free_kib{};

        double ram_used_percent{};
        double swap_used_percent{};
    };

    class MemoryMonitor {
    public:
        MemoryMonitor() = default;

        //Non-blocking single-pass parse of /proc/meminfo.
        bool sample();

        const MemoryMetrics& get_metrics() const noexcept { return metrics_; }

        std::uint64_t get_used_ram_kib() const noexcept {
            return metrics_.total_kib > metrics_.available_kib
                 ? metrics_.total_kib - metrics_.available_kib
                 : 0;
        }

        std::uint64_t get_used_swap_kib() const noexcept {
            return metrics_.swap_total_kib > metrics_.swap_free_kib
                 ? metrics_.swap_total_kib - metrics_.swap_free_kib
                 : 0;
        }

    private:
        MemoryMetrics metrics_{};
    };
}
