#include "temperature.hpp"

#include <algorithm>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace smoothdemon {

    // -- helpers --------------------------------------------------------

    std::string TemperatureMonitor::read_first_line(const std::string& filepath) {
        std::ifstream f(filepath);
        if (!f.is_open()) return {};
        std::string s;
        std::getline(f, s);
        while (!s.empty() &&
               (s.back() == '\n' || s.back() == '\r' ||
                s.back() == ' '  || s.back() == '\t')) {
            s.pop_back();
        }
        return s;       
    }

    bool TemperatureMonitor::read_millideg_celsius(const std::string& filepath,
                                                   double& out_celsius) {
        std::ifstream f(filepath);
        if (!f.is_open()) return false;
        long long milli = 0;
        if (!(f >> milli)) return false;
        out_celsius = static_cast<double>(milli) / 1000.0;
        return true;
    }

    //-------- discovery -------------------------------------------------------------------

    namespace {

        // Classify a (driver, label) pair into a SensorType
        SensorType classify(const std::string& driver, const std::string& label) {
            // CPU package / core
            if (driver == "coretemp" || driver == "k10temp" || driver == "k8temp") {
                if (label.find("Package") != std::string::npos) return SensorType::CpuPackage;
                if (label.find("Tdie")    != std::string::npos) return SensorType::CpuPackage;
                if (label.find("Tctl")    != std::string::npos) return SensorType::CpuPackage;
                if (label.find("Core")    != std::string::npos) return SensorType::CpuCore;
                return SensorType::CpuPackage; // default for coretemp-style drivers
            }

            // Intel iGPU
            if (driver == "i915" || driver == "xe") {
                return SensorType::Gpu;
            }

            // ACPI thermal zones, motherboard diodes, etc.
            return SensorType::Ambient;
        }

    }

    void TemperatureMonitor::discover_sensors() {
        sensors_.clear();

        const fs::path hwmon_root{"/sys/class/hwmon"};
        std::error_code ec;
        if (!fs::exists(hwmon_root, ec)) return;

        for(const auto& entry : fs::directory_iterator(hwmon_root, ec)) {
            if (ec) break;
            const fs::path hwmon_dir = entry.path();

            // Read driver name
            const std::string driver = read_first_line((hwmon_dir / "name").string());
            if (driver.empty()) continue;

            // Walk tempN_input files (N = 1..20)
            for (int n = 1; n <= 20; ++n) {
                const std::string prefix = "temp" + std::to_string(n);
                const fs::path input_path = hwmon_dir / (prefix + "_input");
                std::error_code ec2;
                if (!fs::exists(input_path, ec2)) continue;

                const fs::path label_path = hwmon_dir / (prefix + "_label");
                std::string label = read_first_line(label_path.string());
                if (label.empty()) {
                    label = driver + " " + prefix;
                }

                TempSensor s;
                s.label  = label;
                s.path   = input_path.string();
                s.driver = driver;
                s.type   = classify(driver, label);
                sensors_.push_back(std::move(s));
            }
        }
    }

    // ------ lifecycle -------------------------------------------------------------------------

    TemperatureMonitor::TemperatureMonitor() {
        discover_sensors();
    }

    bool TemperatureMonitor::sample() {
        if (sensors_.empty()) {
            discover_sensors();
            if (sensors_.empty()) return false;
        }

        bool any = false;
        for (auto& s : sensors_) {
            double c = 0.0;
            if (read_millideg_celsius(s.path, c)) {
                s.temp_celsius = c;
                any = true;
            } else {
                s.temp_celsius = -1.0;
            }
        }
        return any;
    }

    // ------- accessors -------------------------------------------------------------------

    double TemperatureMonitor::get_cpu_package_temp() const noexcept {
        //Prefer explicit Package label
        for (const auto& s : sensors_) {
            if (s.type == SensorType::CpuPackage && s.temp_celsius > 0.0) {
                return s.temp_celsius;
            }
        }
        // Fall back to highest core reading
        double best = -1.0;
        for (const auto& s : sensors_) {
            if (s.type == SensorType::CpuCore && s.temp_celsius > best) {
                best = s.temp_celsius;
            }
        }
        return best;
    }

    double TemperatureMonitor::get_gpu_temp() const noexcept {
        for (const auto& s : sensors_) {
            if (s.type == SensorType::Gpu && s.temp_celsius > 0.0) {
                return s.temp_celsius;
            }
        }
        return -1.0;
    }
    
}
