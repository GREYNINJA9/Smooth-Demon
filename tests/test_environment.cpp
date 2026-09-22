#include <chrono>
#include <filesystem>
#include <iostream>

#include <unistd.h>

#include "version.hpp"

namespace fs = std::filesystem;

static bool check_path(const fs::path& p, const char* label) {
    std::cout << "Checking " << label << "...";
    if (fs::exists(p)) {
        std::cout << "OK\n";
        return true;
    }
    std::cout << "MISSING\n";
    return false;
}

int main() {
    std::cout << "=====SmoothDemon Environment Verification =====\n";

    //----- C++ standard -----------------------------------------------
    std::cout << "C++ Standard: " << __cplusplus;
    if (__cplusplus >= 202002L) {
        std::cout << " (C++20 OK)\n";
    } else {
        std::cout << " (C++20 NOT OK - need 202002L or newer)\n";
        return 1;
    }

    //----- POSIX clock tick -------------------------------------------
    long clk_tck = sysconf(_SC_CLK_TCK);
    std::cout << "POSIX CLK_TCK: " << clk_tck << "\n";
    if (clk_tck <= 0) {
        std::cerr << "Failed to query CLK_TCK\n";
        return 2;
    }

    //----- Kernel interfaces ------------------------------------------
    bool ok = true;
    ok &= check_path("/proc/stat",   "/proc/stat");
    ok &= check_path("/proc/meminfo", "/proc/meminfo");

    //----- Dynamic GPU Card Detection ---------------------------------
    std::string detected_gpu_path = "";
    const std::string drm_base_path = "/sys/class/drm/";

    if (std::filesystem::exists(drm_base_path)) {
        for (const auto& entry : std::filesystem::directory_iterator(drm_base_path)) {
            std::string name = entry.path().filename().string();
            // Match base directories like "card0" or "card1" (not "card0-DP-1" or similar)
            if (name.rfind("card", 0) == 0 && name.find("-") == std::string::npos) {
                detected_gpu_path = entry.path().string();
                break; // As we found a valid GPU
            }
        }
    }

    if (!detected_gpu_path.empty()) {
        ok &= check_path(detected_gpu_path.c_str(), detected_gpu_path.c_str());
    } else {
        std::cerr << "Checking /sys/class/drm/card*...MISSING (No GPU interface found)\n";
        ok = false;
    }

    if(!ok) {
        std::cerr << "\nOne or more required kernel interfaces are missing. \n";
        return 4;
    }

    std::cout << "\nAll required kernel interfaces detected successfully.\n";
    return 0;
}
