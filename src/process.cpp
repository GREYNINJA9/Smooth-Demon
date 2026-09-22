#include "process.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>

#include <unistd.h>

namespace smoothdemon {
    ProcessMonitor::ProcessMonitor(std::string target_name)
        : target_name_(std::move(target_name)) {
        const long tck = sysconf(_SC_CLK_TCK);
        if(tck > 0) clk_tck_ = tck;

        const long n = sysconf(_SC_NPROCESSORS_ONLN);
        if (n > 0) num_cores_ = static_cast<int>(n);
    }

    void ProcessMonitor::reset() {
        cached_pid_ = -1;
        prev_ticks_ = 0;
        prev_time_ = {};
        first_sample_ = true;
    }

    //---------Helpers---------------------------------------------

    std::string ProcessMonitor::read_comm(pid_t pid) {
        std::ifstream f("/proc/" + std::to_string(pid) + "/comm");
        if (!f.is_open()) return{};
        std::string name;
        std::getline(f, name);
        //trim trailing newline/CR
        while (!name.empty() &&
               (name.back() == '\n' || name.back() == '\r' ||
                name.back() == ' ' || name.back() == '\t')) {
            name.pop_back();
        }
        return name;
    }

    std::string ProcessMonitor::read_cmdline(pid_t pid) {
        std::ifstream f("/proc/" + std::to_string(pid) + "/cmdline",
                        std::ios::in | std::ios::binary);
        if (!f.is_open()) return {};

        std::string raw;
        f.seekg(0, std::ios::end);
        const auto sz = f.tellg();
        if (sz > 0) {
            raw.resize(static_cast<std::size_t>(sz));
            f.seekg(0, std::ios::beg);
            f.read(raw.data(),sz);
        }

        // Replace NULL with spaces, trim trailing NULL/space
        for (char& c : raw) {
            if (c == '\0') c = ' ';
        }
        while (!raw.empty() && raw.back() == ' ') raw.pop_back();
        return raw;
    }

    bool ProcessMonitor::read_stat(pid_t pid,
                                   char& out_state,
                                   std::uint64_t& out_ticks) {
        std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
        if (!f.is_open()) return false;

        std::string line;
        if (!std::getline(f, line)) return false;

        const std::size_t pos = line.rfind(')');
        if (pos == std::string::npos || pos + 2 >= line.size()) return false;

        out_state = line[pos + 2];
        // Fields after ") " : state is field 3, then fields 4..N follow, space-separated.
        // Field 14 = utime, field 15 = stime.
        // After the state char, we need to skip fields 4..13 (10 fields),
        // then read utime and stime.
        std::istringstream iss(line.substr(pos + 4));

        std::string tmp;
        //Skip fields 4 .. 13 = 10 fields
        for (int i = 0; i < 10; ++i) {
            if (!(iss >> tmp)) return false;
        }

        std::uint64_t utime = 0, stime = 0;
        if (!(iss >> utime >> stime)) return false;

        out_ticks = utime + stime;
        return true;
    }

   bool ProcessMonitor::read_status_memory(pid_t pid,
                                            std::uint64_t& out_rss,
                                            std::uint64_t& out_virt,
                                            std::uint32_t& out_threads) {
        std::ifstream f("/proc/" + std::to_string(pid) + "/status");
        if (!f.is_open()) return false;

        out_rss = 0;
        out_virt = 0;
        out_threads = 0;

        std::string line;
        int found = 0;
        while (std::getline(f, line)) {
            if (line.rfind("VmRSS:", 0) == 0) {
                std::sscanf(line.c_str(), "VmRSS: %llu",
                            reinterpret_cast<unsigned long long*>(&out_rss));
                ++found;
            } else if (line.rfind("VmSize:", 0) == 0) {
                std::sscanf(line.c_str(), "VmSize: %llu",
                            reinterpret_cast<unsigned long long*>(&out_virt));
                ++found;
            } else if (line.rfind("Threads:", 0) == 0) {
                unsigned int th = 0;
                std::sscanf(line.c_str(), "Threads: %u", &th);
                out_threads = static_cast<std::uint32_t>(th);
                ++found;
            }
            if (found >= 3) break;
        }
        return found > 0;
    }

    std::uint32_t ProcessMonitor::count_tasks(pid_t pid) {
        const std::string dir_path = "/proc/" + std::to_string(pid) + "/task";
        DIR* d = opendir(dir_path.c_str());
        if (!d) return 0;

        std::uint32_t count = 0;
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            if (e->d_name[0] >= '0' && e->d_name[0] <= '9') {
                ++count;
            }
        }
        closedir(d);
        return count;
    }

    //----------Target directory----------------------------------------------------

    std::optional<pid_t> ProcessMonitor::find_target_process() {
        DIR* d = opendir("/proc");
        if (!d) return std::nullopt;

        std::optional<pid_t> found;
        struct dirent* e;

        while ((e = readdir(d)) != nullptr) {
            if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;

            const pid_t pid = static_cast<pid_t>(std::atoi(e->d_name));
            if (pid <= 0) continue;

            //Skip self
            if (pid == getpid()) continue;

            const std::string comm = read_comm(pid);
            if (!comm.empty() &&
                (comm == target_name_ ||
                 comm.find(target_name_) != std::string::npos)) {
                found = pid;
                break;
            }

            // Fallback: check cmdline
            const std::string cmd = read_cmdline(pid);
            if (!cmd.empty() &&
                cmd.find(target_name_) != std::string::npos) {
                found = pid;
                break;
            }
        }

        closedir(d);
        return found;
    }

    //---------------Sample-------------------------------------------------

    bool ProcessMonitor::sample(ProcessMetrics& out_metrics) {
        // Locate target if not cached
        if (cached_pid_ <= 0) {
            auto found = find_target_process();
            if (!found) {
                out_metrics = ProcessMetrics{};
                out_metrics.is_running = false;
                return false;
            }
            cached_pid_ = *found;
            first_sample_ = true;
        }

        // Read stat (exits cleanly if target died)
        char state = '?';
        std::uint64_t ticks = 0;
        if (!read_stat(cached_pid_, state, ticks)) {
            reset();
            out_metrics = ProcessMetrics{};
            out_metrics.is_running = false;
            return false;
        }

        // Read memory + threads
        std::uint64_t rss = 0, virt = 0;
        std::uint32_t threads = 0;
        read_status_memory(cached_pid_, rss, virt, threads);

        //Prefer direct task count if status didn't give threads
        if (threads == 0) {
            threads = count_tasks(cached_pid_);
        }

        const auto now = std::chrono::steady_clock::now();

        double cpu_pct = 0.0;

        if (first_sample_) {
            prev_ticks_ = ticks;
            prev_time_ = now;
            first_sample_ = false;
        } else {
            const double dt = std::chrono::duration<double>(now - prev_time_).count();
            if (dt > 0.001) {
                const std::uint64_t dticks = ticks -prev_ticks_;
                cpu_pct = (static_cast<double>(dticks) /
                           static_cast<double>(clk_tck_)) / dt * 100.0;
                prev_ticks_ = ticks;
                prev_time_ = now;
            }
        }

        out_metrics.pid         = cached_pid_;
        out_metrics.name        = read_comm(cached_pid_);
        out_metrics.cmdline     = read_cmdline(cached_pid_);
        out_metrics.state       = state;
        out_metrics.rss_kib     = rss;
        out_metrics.virt_kib    = virt;
        out_metrics.num_threads = threads;
        out_metrics.cpu_percent = cpu_pct;
        out_metrics.is_running  = true;

        return true;
    }
}
