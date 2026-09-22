#include "cpu.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

int main() {
    using namespace smoothdemon;

    std::cout << "=== SmoothDemon CPU Monitor Test ===\n";

    CpuMonitor monitor;
    const std::size_t cores = monitor.get_core_count();
    std::cout << "Detected Cores: " << cores << "\n";

    if (cores == 0) {
        std::cerr << "No logical CPUs detected\n";
        return 1;
    }

    if(!monitor.sample()) {
        std::cerr << "Failed to read /proc/stat (baseline)\n";
        return 1;
    }

    std::cout << "Sampling interval: 500 ms...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if(!monitor.sample()) {
        std::cerr << "Failed to read /proc/stat (second sample)\n";
        return 2;
    }

    const CpuMetrics& agg = monitor.get_aggregate();
    std::cout << "Aggregate CPU: " << agg.total_percent << "% "
              << "(User: "  << agg.user_percent   << "%, "
              << "System: " << agg.system_percent << "%, "
              << "IOwait: " << agg.iowait_percent << "%)\n";
              
    const auto& per = monitor.get_per_core();
    if (per.size() != cores) {
        std::cerr << "Per-core metrics count " << per.size()
                  << " does not match detected cores " << cores << "\n";
        return 3;
    }

    const auto metrics_valid = [](const CpuMetrics& metrics) {
        return std::isfinite(metrics.total_percent) &&
               std::isfinite(metrics.user_percent) &&
               std::isfinite(metrics.system_percent) &&
               std::isfinite(metrics.iowait_percent) &&
               metrics.total_percent >= 0.0 && metrics.total_percent <= 100.0 &&
               metrics.user_percent >= 0.0 && metrics.user_percent <= 100.0 &&
               metrics.system_percent >= 0.0 && metrics.system_percent <= 100.0 &&
               metrics.iowait_percent >= 0.0 && metrics.iowait_percent <= 100.0;
    };

    if (!metrics_valid(agg)) {
        std::cerr << "Invalid aggregate CPU metrics\n";
        return 4;
    }

    for (std::size_t i = 0; i < per.size(); ++i) {
        std::cout << "  Core " << i << ": "
                  << per[i].total_percent << "%\n";
        if (!metrics_valid(per[i])) {
            std::cerr << "Invalid CPU metrics for core " << i << "\n";
            return 5;
        }
    }

    std::cout << "Test passed successfully.\n";
    return 0;
}
