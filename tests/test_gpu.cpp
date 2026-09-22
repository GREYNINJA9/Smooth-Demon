#include "gpu.hpp"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    using namespace smoothdemon;

    std::cout << "=== SmoothDemon Intel GPU Monitor Test ===\n";

    GpuMonitor monitor;
    if (!monitor.initialize()) {
        std::cerr << "Failed to initialize GPU monitor "
                     "(no i915 gt_*_freq_mhz found)\n";
        return 1;
    }

    std::cout << "Device: Intel HD Graphics (i915) at " << monitor.card_path() << "\n";

    // Baseline
    monitor.sample();

    pid_t target_pid = (argc > 1) ? static_cast<pid_t>(std::atoi(argv[1])) : -1;

    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        monitor.sample();

        const auto& m = monitor.get_metrics();
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "[Sample " << (i + 1) << "] "
                  << "Freq: " << m.cur_freq_mhz << " MHz "
                  << "(Act: " << m.act_freq_mhz << " MHz, "
                  << "Bounds: " << m.min_freq_mhz << "-" << m.max_freq_mhz << " MHz) "
                  << "| GPU Activity: " << m.activity_percent << "%";

        if (target_pid > 0) {
            f64 render = 0.0, copy = 0.0;
            if (monitor.sample_process_gpu(target_pid, render, copy)) {
                std::cout << " | PID " << target_pid
                          << " Render: " << render << "%"
                          << " Copy: " << copy << "%";
            } else {
                std::cout << " | PID " << target_pid << " (no DRM fdinfo)";
            }
        }
        std::cout << "\n";
    }

    std::cout << "GPU sampling succeeded.\n";
    return 0;
}
