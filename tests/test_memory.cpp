#include "memory.hpp"

#include <iomanip>
#include <iostream>

int main() {
    using namespace smoothdemon;
    
    std::cout << "=== SmoothDemon Memory Monitor Test ===\n";

    MemoryMonitor monitor;
    if (!monitor.sample()) {
        std::cerr << "Failed to read /proc/meminfo\n";
        return 1;
    }

    const MemoryMetrics& m = monitor.get_metrics();

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "Total RAM:       " << (m.total_kib     / 1024.0) << " MiB\n";
    std::cout << "Available RAM:   " << (m.available_kib / 1024.0) << " MiB\n";
    std::cout << "Free RAM:        " << (m.free_kib      / 1024.0) << " MiB\n";
    std::cout << "Buffers/Cached:  "
              << ((m.buffers_kib + m.cached_kib) / 1024.0) << " MiB\n";

    const std::uint64_t used_ram = monitor.get_used_ram_kib();
    std::cout << "Used RAM:        " << (used_ram / 1024.0) << "MiB ("
              << m.ram_used_percent << "%)\n\n";

    std::cout << "Swap Total:      " << (m.swap_total_kib / 1024.0) << " MiB\n";
    std::cout << "Swap Free:       " << (m.swap_free_kib  / 1024.0) << " MiB\n";
    std::cout << "Swap Used:       " << (monitor.get_used_swap_kib() / 1024.0)
              << " MiB (" << m.swap_used_percent << "%)\n\n";

    // Validation
    if (m.total_kib == 0) {
        std::cerr << "Invalid: total RAM is 0\n";
        return 2;
    }
    if (m.ram_used_percent < 0.0 || m.ram_used_percent > 100.0) {
        std::cerr << "Invalid: ram_used_percent out of range\n";
        return 3;
    }
    if (m.swap_total_kib > 0 &&
        (m.swap_used_percent < 0.0 || m.swap_used_percent > 100.0)) {
        std::cerr << "Invalid: swap_used_percent out of range\n";
        return 4;
    }

    std::cout << "Memory sampling succeeded.\n";
    return 0;
}
