#include "cpu.hpp"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
    using namespace smoothdemon;

    std::cout << "=== SmoothDemon CPU Monitor Test ===\n";

    CpuMonitor monitor;
    const std::size_t cores = monitor.get_core_count();
    std::cout << "Detected Cores: " << cores << "\n";

    if(!monitor.sample()) {
        std::cerr << "Failed to read /proc/stat (baseline)\n";
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
    for (std::size_t i = 0; i < per.size(); ++i) {
        std::cout << "  Core " << i << ": "
                  << per[i].total_percent << "%\n";
    }

    if (cores != 2) {
        std::cerr << "Unexpected core count: " << cores << "\n";
        return 3;
    }

    std::cout << "Test passed successfully.\n";
    return 0;
}
