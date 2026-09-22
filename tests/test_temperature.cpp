#include "temperature.hpp"

#include <iomanip>
#include <iostream>

static const char* type_name(smoothdemon::SensorType t) {
    using smoothdemon::SensorType;
    switch (t) {
        case SensorType::CpuPackage: return "CPU-Package";
        case SensorType::CpuCore:    return "CPU-Core";
        case SensorType::Gpu:        return "GPU";
        case SensorType::Ambient:    return "Ambient";
    }
    return "?";
}

int main() {
    using namespace smoothdemon;

    std::cout << "=== SmoothDemon Temperature Monitor Test ===\n";

    TemperatureMonitor monitor;
    const auto& sensors = monitor.get_sensors();

    std::cout << "Discovered " << sensors.size() << " thermal sensors:\n";

    if (!monitor.sample()) {
        std::cerr << "Failed to sample any sensor\n";
        return 1;
    }

    std::cout << std::fixed << std::setprecision(1);

    for (std::size_t i = 0; i < sensors.size(); ++i) {
        const auto& s = sensors[i];
        std::cout << " [" << i << "] "
                  << std::left << std::setw(22) << s.label
                  << " (" << std::setw(11) << type_name(s.type) << ") "
                  << s.temp_celsius << " °C"
                  << " [" << s.path << "]\n";
    }

    const double cpu = monitor.get_cpu_package_temp();
    const double gpu = monitor.get_gpu_temp();

    std::cout << "\nPrimary CPU Package: " << (cpu < 0 ? -1.0 : cpu) << " °C\n";
    std::cout << "Primary GPU Temp:      " << (gpu < 0 ? -1.0 : gpu) << " °C\n";

    //Validate
    bool any_cpu = false;
    for (const auto& s : sensors) {
        if (s.type == SensorType::CpuPackage || s.type == SensorType::CpuCore) {
            any_cpu = true;
            if (s.temp_celsius < 15.0 || s.temp_celsius > 110.0) {
                std::cerr << "CPU temp out of plausible range: "
                          << s.temp_celsius << "\n";
                return 2;
            }
        }
    }

    if (!any_cpu) {
        std::cerr << "No CPU thermal sensor discovered\n";
        return 3;
    }

    std::cout << "Temperature sampling succeeded.\n";
    return 0;
}
