#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include <sys/types.h>

namespace smoothdemon {
    struct ProcessMetrics {
        pid_t pid{-1};
        std::string name{};
        std::string cmdline{};
        char state{'?'};
        std::uint64_t rss_kib{0};
        std::uint64_t virt_kib{0};
        std::uint32_t num_threads{0};
        double cpu_percent{0.0};
        bool is_running{false};
    };

    class ProcessMonitor {
    public:
        explicit ProcessMonitor(std::string target_name = "ryujinx");

        // Scan /proc for the target process. Returns PID if found.
        std::optional<pid_t> find_target_process();

        //Non-blocking sample. Returns false if target is not running.
        bool sample(ProcessMetrics& out_metrics);

        //Clear cached state (e.g. after target exists).
        void reset();

        const std::string& target_name() const noexcept { return target_name_; }

    private:
        std::string target_name_;
        pid_t cached_pid_{-1};
        long clk_tck_{100};
        int num_cores_{1};

        std::uint64_t prev_ticks_{0};
        std::chrono::steady_clock::time_point prev_time_{};
        bool first_sample_{true};

        static std::string read_comm(pid_t pid);
        static std::string read_cmdline(pid_t pid);
        static bool read_stat(pid_t pid, char& out_state, std::uint64_t& out_ticks);
        static bool read_status_memory(pid_t pid,
                                       std::uint64_t& out_rss,
                                       std::uint64_t& out_virt,
                                       std::uint32_t& out_threads);
        static std::uint32_t count_tasks(pid_t pid);
    };
}
