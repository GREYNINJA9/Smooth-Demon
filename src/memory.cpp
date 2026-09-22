#include "memory.hpp"

#include <cstdio>
#include <fstream>
#include <string>

namespace smoothdemon {

    bool MemoryMonitor::sample() {
        std::ifstream f("/proc/meminfo");
        if (!f.is_open()) return false;

        MemoryMetrics data{};
        int fields_found = 0;

        std::string line;
        line.reserve(128);

        unsigned long long v = 0;

        while (std::getline(f, line)) {
            if (line.rfind("MemTotal:", 0) == 0) {
                if (std::sscanf(line.c_str(), "MemTotal: %llu kB", &v) == 1) {
                    data.total_kib = v;
                    ++fields_found;
                }
            } else if (line.rfind("MemFree:", 0) == 0) {
                if (std::sscanf(line.c_str(), "MemFree: %llu kB", &v) == 1) {
                    data.free_kib = v;
                    ++fields_found;
                }
            } else if (line.rfind("MemAvailable:", 0) == 0) {
                if (std::sscanf(line.c_str(), "MemAvailable: %llu kB", &v) == 1) {
                    data.available_kib = v;
                    ++fields_found;
                }
            } else if (line.rfind("Buffers:", 0) == 0) {
                if (std::sscanf(line.c_str(), "Buffers: %llu kB", &v) == 1) {
                    data.buffers_kib = v;
                    ++fields_found;
                }
            } else if (line.rfind("Cached:", 0) == 0) {
                if (std::sscanf(line.c_str(), "Cached: %llu kB", &v) == 1) {
                    data.cached_kib = v;
                    ++fields_found;
                }
            } else if (line.rfind("SwapTotal:", 0) == 0) {
                if (std::sscanf(line.c_str(), "SwapTotal: %llu kB", &v) == 1) {
                    data.swap_total_kib = v;
                    ++fields_found;
                }
            } else if (line.rfind("SwapFree:", 0) == 0) {
                if (std::sscanf(line.c_str(), "SwapFree: %llu kB", &v) == 1) {
                    data.swap_free_kib = v;
                    ++fields_found;
                }
            }

            if (fields_found >= 7) break;
        }

        if (data.total_kib == 0) return false;

        //Safety clamp
        if (data.available_kib > data.total_kib) {
            data.available_kib = data.total_kib;
        }

        const std::uint64_t used_ram = data.total_kib - data.available_kib;
        data.ram_used_percent =
            (static_cast<double>(used_ram) / static_cast<double>(data.total_kib)) * 100.0;

        if (data.swap_total_kib > 0) {
            std::uint64_t used_swap = 0;
            if (data.swap_free_kib <= data.swap_total_kib) {
                used_swap = data.swap_total_kib - data.swap_free_kib;
            } else {
                //SwapFree > SwapTotal shouldn't happen, but guard anyway
                data.swap_free_kib = data.swap_total_kib;
            }
            data.swap_used_percent =
                (static_cast<double>(used_swap) / static_cast<double>(data.swap_total_kib)) * 100.0;
        } else {
            data.swap_used_percent = 0.0;
        }

        metrics_ = data;
        return true;
    }
}
