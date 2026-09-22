#include "system_info.hpp"

#include <iomanip>
#include <iostream>

int main() {
    using namespace smoothdemon;

    std::cout << "=== SmoothDemon System Information ===\n";

    const SystemMetadata info = SystemInfoCollector::collect();

    std::cout << std::left;
    std::cout << std::setw(20) << "Operating System:" << info.os_name << "\n";
    std::cout << std::setw(20) << "Kernel Release:"
              << info.kernel_version << " (" << info.architecture << ")\n";
    std::cout << std::setw(20) << "Processor:"
              << info.cpu_model
              << " (" << info.cpu_cores_physical << " cores, "
              << info.cpu_cores_logical << " threads)\n";
    std::cout << std::setw(20) << "Graphics:"
              << info.gpu_model << " [" << info.gpu_pci_id << "]\n";
    std::cout << std::setw(20) << "Kernel Driver:" << info.driver_name << "\n";
    std::cout << std::setw(20) << "Display Server:" << info.display_protocol << "\n";
    std::cout << std::setw(20) << "Compositor:" << info.compositor << "\n";
    std::cout << std::setw(20) << "System Uptime:"
              << SystemInfoCollector::get_formatted_uptime() << "\n";

    // Validation
    auto fail = [](const char* field) {
        std::cerr << "FAIL: empty field: " << field << "\n";
        return 1;
    };

    if (info.os_name.empty())            return fail("os_name");
    if (info.kernel_version.empty())     return fail("kernel_version");
    if (info.architecture.empty())       return fail("architecture");
    if (info.cpu_model.empty())          return fail("cpu_model");
    if (info.gpu_model.empty())          return fail("gpu_model");
    if (info.gpu_pci_id.empty())         return fail("gpu_pci_id");
    if (info.display_protocol.empty())   return fail("display_protocol");
    if (info.compositor.empty())         return fail("compositor");
    if (info.cpu_cores_logical == 0)     return fail("cpu_cores_logical");

    std::cout << "System information collection succeeded.\n";
    return 0;
}
