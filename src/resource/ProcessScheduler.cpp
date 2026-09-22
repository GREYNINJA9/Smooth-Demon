#include "resource/ProcessScheduler.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include <dirent.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

#ifndef SCHED_ISO
#define SCHED_ISO 4
#endif

namespace smoothdemon {

    std::vector<pid_t> ProcessScheduler::GetProcessThreads(pid_t pid) {
        std::vector<pid_t> tids;

        const std::string path = "/proc/" + std::to_string(pid) + "/task";
        DIR* d = opendir(path.c_str());
        if (!d) return tids;

        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
            const long tid = std::strtol(e->d_name, nullptr, 10);
            if (tid > 0) tids.push_back(static_cast<pid_t>(tid));
        }
        closedir(d);
        return tids;
    }

    bool ProcessScheduler::ReniceProcessThreads(pid_t pid, int niceValue) {
        if (niceValue < -20) niceValue = -20;
        if (niceValue >  19) niceValue =  19;

        const auto tids = GetProcessThreads(pid);
        if (tids.empty()) return false;

        bool any_ok = false;
        int eperm_count = 0;

        for (pid_t tid : tids) {
            errno = 0;
            if (setpriority(PRIO_PROCESS, static_cast<id_t>(tid), niceValue) == 0) {
                any_ok = true;
            } else {
                if (errno == EPERM) {
                    ++eperm_count;
                }
                // ESRCH thread exited mid-iteration - silently skip
            }
        }

        if (!any_ok && eperm_count > 0) {
            std::fprintf(stderr,
                "[sched] renice(%d -> %d) failed on %d thread(s): EPERM. "
                "Grant CAP_SYS_NICE or add PAM limits.\n",
                pid, niceValue, eperm_count);
        }
        return any_ok;
    }

    int ProcessScheduler::GetAverageNice(pid_t pid) {
        const auto tids = GetProcessThreads(pid);
        if (tids.empty()) return -128;

        long sum = 0;
        int count = 0;
        for (pid_t tid : tids) {
            errno = 0;
            const int p = getpriority(PRIO_PROCESS, static_cast<id_t>(tid));
            if (p == -1 && errno) continue;
            sum += p;
            ++count;
        }
        if (count == 0) return -128;
        return static_cast<int>(sum / count);
    }

    int ProcessScheduler::GetLogicalCpuCount() {
        const long n = sysconf(_SC_NPROCESSORS_ONLN);
        return (n > 0) ? static_cast<int>(n) : 1;
    }

    bool ProcessScheduler::ApplySchedulingPolicy(pid_t pid, int policy) {
        const int cores = GetLogicalCpuCount();

        // Dual-core (and single-core) safety guard: refuse real-time policies
        if (cores <= 2 && (policy == SCHED_FIFO || policy == SCHED_RR)) {
            std::fprintf(stderr,
                "[sched] REFUSED %s on %d-core CPU — this would starve "
                "i915/hyprland threads. Use SCHED_OTHER with elevated nice.\n",
                (policy == SCHED_FIFO) ? "SCHED_FIFO" : "SCHED_RR", cores);
            return false;
        }

        struct sched_param sp{};
        sp.sched_priority = 0;   // required 0 for SCHED_OTHER/BATCH/IDLE

        if (policy == SCHED_FIFO || policy == SCHED_RR) {
            sp.sched_priority = 1;   // lowest RT priority — leave headroom for kernel threads
        }

        if (sched_setscheduler(pid, policy, &sp) != 0) {
            std::fprintf(stderr,
                "[sched] sched_setscheduler(pid=%d, policy=%d) failed: %s\n",
                pid, policy, std::strerror(errno));
            return false;
        }
        return true;
    }

    bool ProcessScheduler::RestoreDefaultScheduling(pid_t pid) {
        // Back to SCHED_OTHER with nice 0 on all threads
        const auto tids = GetProcessThreads(pid);
        for (pid_t tid : tids) {
            errno = 0;
            setpriority(PRIO_PROCESS, static_cast<id_t>(tid), 0);
        }
        return ApplySchedulingPolicy(pid, SCHED_OTHER);
    }

}
