#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace smoothdemon {
    struct CpuSample {
        std::uint64_t user{};
        std::uint64_t nice{};
        std::uint64_t system{};
        std::uint64_t idle{};
        std::uint64_t iowait{};
        std::uint64_t irq{};
        std::uint64_t softirq{};
        std::uint64_t steal{};
    };

    struct CpuMetrics {
        double total_percent{};
        double user_percent{};
        double system_percent{};
        double iowait_percent{};
    };

    class CpuMonitor {
    public:
        CpuMonitor();

        // Reads /proc/stat, computes deltas against previous sample.
        // First call establishes baseline and returns true with metrics = 0.
        // Non-blocking, zero heap allocation on the hot path.
        bool sample();

        const CpuMetrics& get_aggregate() const noexcept { return curr_aggregate_; }
        const std::vector<CpuMetrics>& get_per_core() const noexcept { return curr_cores_; }
        std::size_t get_core_count() const noexcept { return prev_cores_.size(); }

    private:
        bool first_sample_{true};

        CpuSample prev_aggregate_{};
        std::vector<CpuSample> prev_cores_;

        CpuMetrics curr_aggregate_{};
        std::vector<CpuMetrics> curr_cores_;

        static CpuSample calculate_delta(const CpuSample& prev,
                                         const CpuSample& curr) noexcept;
        static CpuMetrics calculate_metrics(const CpuSample& delta) noexcept;
        static std::uint64_t get_total_ticks(const CpuSample& s) noexcept;
    };
}
