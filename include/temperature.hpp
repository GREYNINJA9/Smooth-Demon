#pragma once

#include <string>
#include <vector>

namespace smoothdemon {

    enum class SensorType {
        CpuPackage,
        CpuCore,
        Gpu,
        Ambient,
    };

    struct TempSensor {
        std::string label;
        std::string path;        // /sys/class/hwmon/hwmonX/tempY_input
        std::string driver;      // coretemp, i915, acpitz, ...
        SensorType  type{SensorType::Ambient};
        double      temp_celsius{0.0};
    };

    class TemperatureMonitor {
    public:
        TemperatureMonitor();

        //Scan /sys/class/hwmon once, cache resolved paths.
        void discover_sensors();

        // Read all cached sensor paths (no directory iteration)
        bool sample();

        const std::vector<TempSensor>& get_sensors() const noexcept { return sensors_; }

        // Returns -1.0 if no CPU package sensor was found
        double get_cpu_package_temp() const noexcept;
        // Returns -1.0 if no GPU sensor was found
        double get_gpu_temp() const noexcept;

    private:
        std::vector<TempSensor> sensors_;

        static std::string read_first_line(const std::string& filepath);
        static bool read_millideg_celsius(const std::string& filepath, double& out_celsius);
    };
    
}
