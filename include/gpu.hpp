#pragma once

#include "types.hpp"

#include <chrono>
#include <string>
#include <unordered_map>

#include <sys/types.h>

namespace smoothdemon {

    struct GpuMetrics {
        u32 cur_freq_mhz{0};
        u32 act_freq_mhz{0};
        u32 min_freq_mhz{0};
        u32 max_freq_mhz{0};
        f64 activity_percent{0.0};
        f64 process_render_percent{0.0};
        f64 process_copy_percent{0.0};
        u64 shared_mem_kib{0};
        bool is_available{false};
    };

    class GpuMonitor {
    public:
        GpuMonitor() = default;

        // Scans /sys/class/drm/card* for the i915 device exposing gt_*_freq_mhz
        bool initialize();

        // Reads frequencies + RC6 residency (no directory scans)
        bool sample();

        // Scans /proc/<pid>/fdinfo/* for drm-engine-{render,copy} deltas
        bool sample_process_gpu(pid_t pid, f64& out_render_pct, f64& out_copy_pct);

        const GpuMetrics& get_metrics() const noexcept { return metrics_; }
        const std::string& card_path() const noexcept { return drm_card_path_; }

    private:
        struct ProcessGpuState {
            u64 prev_render_ns{0};
            u64 prev_copy_ns{0};
            std::chrono::steady_clock::time_point prev_time{};
            bool first_sample{true};
        };

        std::string drm_card_path_;
        u64 prev_rc6_ms_{0};
        std::chrono::steady_clock::time_point prev_time_{};
        bool first_sample_{true};

        GpuMetrics metrics_{};
        std::unordered_map<pid_t, ProcessGpuState> process_states_;

        static u32 read_sysfs_u32(const std::string& filepath);
        static u64 read_sysfs_u64(const std::string& filepath);
    };

}
