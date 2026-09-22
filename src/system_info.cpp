#include "system_info.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <utility>

#include <sys/utsname.h>
#include <unistd.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace smoothdemon {

    // --------- small string helpers --------------------------------------------------------------------------

    namespace {

        inline std::string trim(const std::string& s) {
            std::size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t' ||
                            s[a] == '\n' || s[a] == '\r')) ++a;
            while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' ||
                            s[b-1] == '\n' || s[b-1] == '\r')) --b;
            return s.substr(a, b - a);
        }

        inline std::string strip_quotes(const std::string& s) {
            std::string t = trim(s);
            if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
                t = t.substr(1, t.size() - 2);
            }
            return t;
        }

        inline bool is_card_dir_name(const std::string& name) {
            if (name.size() < 5) return false;
            if (name.rfind("card", 0) != 0) return false;
            for (std::size_t i = 4; i < name.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(name[i]))) return false;
            }
            return true;
        }

    }

    // --------- parse_os_name --------------------------------------------------------------------------------------

    std::string SystemInfoCollector::parse_os_name() {
        const char* paths[] = {"/etc/os-release", "/usr/lib/os-release"};

        for (const char* path : paths) {
            std::ifstream f(path);
            if (!f.is_open()) continue;

            std::string line;
            while (std::getline(f, line)) {
                if (line.rfind("PRETTY_NAME=", 0) == 0) {
                    std::string v = line.substr(12);
                    return strip_quotes(v);
                }
            }
        }
        return "Unknown Linux";
    }

    // --------- parse_cpu_model --------------------------------------------------------------------

    std::string SystemInfoCollector::parse_cpu_model() {
        std::ifstream f("/proc/cpuinfo");
        if (!f.is_open()) return "Unknown CPU";

        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("model name", 0) == 0) {
                const auto colon = line.find(':');
                if (colon != std::string::npos) {
                    return trim(line.substr(colon + 1));
                }
            }
        }
        return "Unknown CPU";
    }

    // --------- parse_cpu_physical_cores -----------------------------------------------

    u32 SystemInfoCollector::parse_cpu_physical_cores() {
        std::ifstream f("/proc/cpuinfo");
        if (!f.is_open()) return 0;

        std::set<std::pair<int,int>> cores;
        int physical_id = -1;
        int core_id     = -1;
        std::string line;

        auto flush = [&]() {
            if (physical_id >= 0 && core_id >= 0) {
                cores.insert({physical_id, core_id});
            }
            physical_id = core_id = -1;
        };

        while (std::getline(f, line)) {
            if (line.empty()) { flush(); continue; }

            const auto colon = line.find(':');
            if (colon == std::string::npos) continue;

            std::string key = trim(line.substr(0, colon));
            std::string val = trim(line.substr(colon + 1));

            if (key == "physical id") {
                try { physical_id = std::stoi(val); } catch (...) {}
            } else if (key == "core id") {
                try { core_id = std::stoi(val); } catch (...) {}
            }
        }
        flush();

        return static_cast<u32>(cores.size());
    }

    // ---------- parse_gpu_info -----------------------------------------------------------------------

    void SystemInfoCollector::parse_gpu_info(std::string& out_model,
                                            std::string& out_pci_id) {
        out_model.clear();
        out_pci_id.clear();

        std::error_code ec;
        const fs::path drm_root{"/sys/class/drm"};
        if (!fs::exists(drm_root, ec)) {
            out_model  = "Not Detected";
            out_pci_id = "unknown";
            return;
        }

        std::string vendor, device;

        for (const auto& entry : fs::directory_iterator(drm_root, ec)) {
            if (ec) break;

            const std::string name = entry.path().filename().string();
            if (!is_card_dir_name(name)) continue;

            const fs::path vendor_path = entry.path() / "device" / "vendor";
            const fs::path device_path = entry.path() / "device" / "device";

            if (!fs::exists(vendor_path, ec)) continue;

            std::ifstream vf(vendor_path.string());
            if (!vf.is_open()) continue;
            vf >> vendor;

            std::ifstream df(device_path.string());
            if (df.is_open()) df >> device;

            break;   // take the first matching card (card1 on your system)
        }

        if (vendor.empty()) {
            out_model  = "Not Detected";
            out_pci_id = "unknown";
            return;
        }

        // Strip "0x" prefix
        if (vendor.rfind("0x", 0) == 0) vendor = vendor.substr(2);
        if (device.rfind("0x", 0) == 0) device = device.substr(2);

        // Normalize to lowercase
        auto lower = [](std::string& s) {
            for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
        };
        lower(vendor);
        lower(device);

        out_pci_id = vendor + ":" + device;

        // Intel (0x8086) — decode common Gen9 GT1/GT2/GT3 SKUs
        if (vendor == "8086") {
            if (device == "5906" || device == "5902") {
                out_model = "Intel HD Graphics 610 (Kaby Lake GT1)";
            } else if (device == "5916" || device == "5917") {
                out_model = "Intel HD Graphics 615 (Kaby Lake GT2)";
            } else if (device == "5926" || device == "5927") {
                out_model = "Intel Iris Plus Graphics 640/650 (Kaby Lake GT3)";
            } else if (device == "591b" || device == "591d") {
                out_model = "Intel HD Graphics 620 (Kaby Lake GT2)";
            } else {
                out_model = "Intel Integrated Graphics (device " + device + ")";
            }
        } else {
            out_model = "Unknown GPU (" + out_pci_id + ")";
        }
    }

    // ------- detect_display_session ----------------------------------------------------------------------------

    void SystemInfoCollector::detect_display_session(std::string& out_protocol,
                                                    std::string& out_compositor) {
        out_protocol.clear();
        out_compositor.clear();

        // Display protocol
        if (const char* st = std::getenv("XDG_SESSION_TYPE")) {
            out_protocol = st;
        } else if (std::getenv("WAYLAND_DISPLAY")) {
            out_protocol = "wayland";
        } else if (std::getenv("DISPLAY")) {
            out_protocol = "x11";
        } else {
            out_protocol = "unknown";
        }

        // Compositor
        if (std::getenv("HYPRLAND_INSTANCE_SIGNATURE")) {
            out_compositor = "Hyprland";
        } else if (const char* d = std::getenv("XDG_CURRENT_DESKTOP")) {
            out_compositor = d;
        } else if (const char* d = std::getenv("DESKTOP_SESSION")) {
            out_compositor = d;
        } else {
            out_compositor = "Unknown";
        }
    }

    // ----------- collect --------------------------------------------------------------------------------

    SystemMetadata SystemInfoCollector::collect() {
        SystemMetadata meta{};

        // Kernel + architecture via uname()
        struct utsname uts {};
        if (uname(&uts) == 0) {
            meta.kernel_version = uts.release;
            meta.architecture   = uts.machine;
        } else {
            meta.kernel_version = "unknown";
            meta.architecture   = "unknown";
        }

        meta.os_name            = parse_os_name();
        meta.cpu_model          = parse_cpu_model();
        meta.cpu_cores_physical = parse_cpu_physical_cores();

        const long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
        meta.cpu_cores_logical = (ncpu > 0) ? static_cast<u32>(ncpu) : 0;

        parse_gpu_info(meta.gpu_model, meta.gpu_pci_id);
        meta.driver_name = "i915";

        detect_display_session(meta.display_protocol, meta.compositor);

        return meta;
    }

    // ---------------- uptime ------------------------------------------------------------------------------

    u64 SystemInfoCollector::get_uptime_seconds() {
        std::ifstream f("/proc/uptime");
        if (!f.is_open()) return 0;
        double secs = 0.0;
        if (!(f >> secs)) return 0;
        if (secs < 0.0) secs = 0.0;
        return static_cast<u64>(secs);
    }

    std::string SystemInfoCollector::get_formatted_uptime() {
        const u64 secs = get_uptime_seconds();

        const u64 days    = secs / 86400;
        const u64 hours   = (secs % 86400) / 3600;
        const u64 minutes = (secs % 3600)  / 60;
        const u64 seconds = secs % 60;

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%llud %02lluh:%02llum:%02llus",
                    static_cast<unsigned long long>(days),
                    static_cast<unsigned long long>(hours),
                    static_cast<unsigned long long>(minutes),
                    static_cast<unsigned long long>(seconds));
        return std::string(buf);
    }

}
