#include "gpu.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace smoothdemon {

    // ------- helpers --------------------------------------------------------------

    u32 GpuMonitor::read_sysfs_u32(const std::string& filepath) {
        std::ifstream f(filepath);
        if (!f.is_open()) return 0;
        u64 v = 0;
        if (!(f >> v)) return 0;
        if (v > 0xFFFFFFFFull) return 0xFFFFFFFFu;
        return static_cast<u32>(v);
    }

    u64 GpuMonitor::read_sysfs_u64(const std::string& filepath) {
        std::ifstream f(filepath);
        if (!f.is_open()) return 0;
        u64 v = 0;
        if (!(f >> v)) return 0;
        return v;
    }

    // ----------- initialize ----------------------------------------------------------------------

    bool GpuMonitor::initialize() {
        drm_card_path_.clear();
        metrics_ = GpuMetrics{};
        first_sample_ = true;

        std::error_code ec;
        const fs::path drm_root{"/sys/class/drm"};
        if (!fs::exists(drm_root, ec)) return false;

        for (const auto& entry : fs::directory_iterator(drm_root, ec)) {
            if (ec) break;

            const std::string name = entry.path().filename().string();
            // Must be exactly "card" + digits (skip "card1-DP-1" etc.)
            if (name.size() < 5 || name.rfind("card", 0) != 0) continue;
            bool digits_only = true;
            for (std::size_t i = 4; i < name.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
                    digits_only = false;
                    break;
                }
            }
            if (!digits_only) continue;

            const fs::path probe = entry.path() / "gt_cur_freq_mhz";
            if (fs::exists(probe, ec)) {
                drm_card_path_ = entry.path().string();
                break;
            }
        }

        if (drm_card_path_.empty()) return false;

        metrics_.min_freq_mhz = read_sysfs_u32(drm_card_path_ + "/gt_min_freq_mhz");
        metrics_.max_freq_mhz = read_sysfs_u32(drm_card_path_ + "/gt_max_freq_mhz");
        metrics_.is_available = true;
        return true;
    }

    // ---------- system sample ---------------------------------------------------------------------------------------

    bool GpuMonitor::sample() {
        if (!metrics_.is_available) {
            if (!initialize()) return false;
        }

        metrics_.cur_freq_mhz = read_sysfs_u32(drm_card_path_ + "/gt_cur_freq_mhz");
        metrics_.act_freq_mhz = read_sysfs_u32(drm_card_path_ + "/gt_act_freq_mhz");
        if (metrics_.act_freq_mhz == 0) {
            metrics_.act_freq_mhz = metrics_.cur_freq_mhz;
        }

        // Optional: GTT/shared memory (may not exist on integrated)
        u64 gtt_used_bytes = read_sysfs_u64(
            drm_card_path_ + "/device/mem_info_gtt_used");
        metrics_.shared_mem_kib = gtt_used_bytes / 1024;

        const u64 rc6_ms = read_sysfs_u64(drm_card_path_ + "/power/rc6_residency_ms");
        const auto now = std::chrono::steady_clock::now();

        if (first_sample_) {
            prev_rc6_ms_ = rc6_ms;
            prev_time_ = now;
            first_sample_ = false;
            metrics_.activity_percent = 0.0;
            return true;
        }

        const f64 dt_ms = std::chrono::duration<f64, std::milli>(now - prev_time_).count();
        if (dt_ms > 1.0) {
            const u64 d_rc6 = rc6_ms - prev_rc6_ms_;    // unsigned wrap safe
            const f64 active_ms = dt_ms - static_cast<f64>(d_rc6);
            metrics_.activity_percent = std::clamp(active_ms / dt_ms * 100.0, 0.0, 100.0);
            prev_rc6_ms_ = rc6_ms;
            prev_time_ = now;
        }
        return true;
    }

    // --------- per-process sample -----------------------------------------------------------------------------------

    bool GpuMonitor::sample_process_gpu(pid_t pid,
                                    f64& out_render_pct,
                                    f64& out_copy_pct) {
        out_render_pct = 0.0;
        out_copy_pct = 0.0;

        const std::string dir_path = "/proc/" + std::to_string(pid) + "/fdinfo";
        DIR* d = opendir(dir_path.c_str());
        if (!d) {
            if (errno == EACCES) {
                // Yama ptrace_scope blocks same-user fdinfo access.
                // Set via: echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
                static bool warned = false;
                if (!warned) {
                    std::fprintf(stderr,
                        "[gpu] EACCES on %s — ptrace_scope blocks fdinfo. "
                        "Run: sudo sysctl kernel.yama.ptrace_scope=0\n",
                        dir_path.c_str());
                    warned = true;
                }
            }
            return false;
        }
        // Deduplicate by drm-client-id: multiple fds may share one client and
        // report identical cumulative engine times
        // client-id -> {render_ns, copy_ns}
        struct ClientEntry { u64 render_ns{0}; u64 copy_ns{0}; };
        std::unordered_map<u64, ClientEntry> clients;

        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;

            const std::string fpath = dir_path + "/" + e->d_name;
            std::ifstream f(fpath);
            if (!f.is_open()) continue;

            u64 client_id = 0;
            u64 render_ns = 0;
            u64 copy_ns   = 0;
            bool has_client = false;
            bool is_i915    = false;

            std::string line;
            while (std::getline(f, line)) {
                if (line.rfind("drm-driver:", 0) == 0) {
                    if (line.find("i915") != std::string::npos ||
                        line.find("xe")   != std::string::npos) {
                        is_i915 = true;
                    }
                } else if (line.rfind("drm-client-id:", 0) == 0) {
                    unsigned long long v = 0;
                    if (std::sscanf(line.c_str(), "drm-client-id:\t%llu", &v) == 1 ||
                        std::sscanf(line.c_str(), "drm-client-id: %llu",  &v) == 1) {
                        client_id = static_cast<u64>(v);
                        has_client = true;
                    }
                } else if (line.rfind("drm-engine-render:", 0) == 0) {
                    unsigned long long v = 0;
                    const char* colon = std::strchr(line.c_str(), ':');
                    if (colon && std::sscanf(colon + 1, " %llu", &v) == 1)
                        render_ns = static_cast<u64>(v);
                } else if (line.rfind("drm-engine-copy:", 0) == 0) {
                    unsigned long long v = 0;
                    const char* colon = std::strchr(line.c_str(), ':');
                    if (colon && std::sscanf(colon + 1, " %llu", &v) == 1)
                        copy_ns = static_cast<u64>(v);
                }
            }

            if (!is_i915 || !has_client) continue;

            // Keep the largest value seen per client (all fds of the same client
            // should report identical values, but guard against races)
            auto& slot = clients[client_id];
            if (render_ns > slot.render_ns) slot.render_ns = render_ns;
            if (copy_ns   > slot.copy_ns)   slot.copy_ns   = copy_ns;
        }
        closedir(d);

        if (clients.empty()) return false;

        // Sum render/copy across unique clients
        u64 total_render_ns = 0;
        u64 total_copy_ns   = 0;
        for (const auto& [cid, entry] : clients) {
            total_render_ns += entry.render_ns;
            total_copy_ns   += entry.copy_ns;
        }

        ProcessGpuState& state = process_states_[pid];
        const auto now = std::chrono::steady_clock::now();

        if (state.first_sample) {
            state.prev_render_ns = total_render_ns;
            state.prev_copy_ns   = total_copy_ns;
            state.prev_time      = now;
            state.first_sample   = false;
            return true;
        }

        const f64 dt = std::chrono::duration<f64>(now - state.prev_time).count();
        if (dt > 0.001) {
            const u64 d_render = total_render_ns - state.prev_render_ns;
            const u64 d_copy   = total_copy_ns   - state.prev_copy_ns;

            out_render_pct = std::clamp(
                (static_cast<f64>(d_render) / 1e9) / dt * 100.0, 0.0, 100.0);
            out_copy_pct = std::clamp(
                (static_cast<f64>(d_copy) / 1e9) / dt * 100.0, 0.0, 100.0);

            state.prev_render_ns = total_render_ns;
            state.prev_copy_ns   = total_copy_ns;
            state.prev_time      = now;
        }
        return true;
    }
}
