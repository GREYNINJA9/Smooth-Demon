#include "process.hpp"

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    using namespace smoothdemon;

    const std::string target = (argc > 1) ? argv[1] : "ryujinx";

    std::cout << "=== SmoothDemon Process Monitor Test ===\n";
    std::cout << "Target: " << target << "\n";

    ProcessMonitor monitor(target);

    for (int i = 0; i < 5; ++i) {
        ProcessMetrics m;
        const bool ok = monitor.sample(m);

        std::cout << "[Sample " << (i + 1) << "] ";

        if (!ok) {
            std::cout << "Target process not found. Waiting...\n";
        } else {
            std::cout << std::fixed << std::setprecision(1);
            std::cout << "PID: " << m.pid
                      << " | Name: " << m.name
                      << " | State: " << m.state
                      << " | Threads: " << m.num_threads
                      << " | RSS: " << (m.rss_kib / 1024.0) << " MiB"
                      << " | Virt: " << (m.virt_kib / 1024.0) << " MiB"
                      << " | CPU: " << m.cpu_percent << "%";
            if (m.cpu_percent == 0.0 && i ==0) {
                std::cout << " (baseline)";
            }
            std::cout << "\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "Process monitoring active.\n";
    return 0;
}
