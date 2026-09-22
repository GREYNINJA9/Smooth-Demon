#pragma once

#include "types.hpp"

#include <vector>

#include <sys/types.h>

namespace smoothdemon {

    class ProcessScheduler {
    public:
        // Enumerate all thread IDs of a process by scanning /proc/<pid>/task/
        static std::vector<pid_t> GetProcessThreads(pid_t pid);

        // Apply nice value (-20..19) to every thread of the target process
        // Returns true if at least one thread was successfully modified
        // Skips ESRCH silently (thread exited mid-iteration)
        static bool ReniceProcessThreads(pid_t pid, int niceValue);

        // Query average nice value across all threads of the process
        // Returns -128 on error
        static int GetAverageNice(pid_t pid);

        // Apply scheduling policy to a process
        // On CPUs with <= 2 logical cores, SCHED_FIFO / SCHED_RR are REFUSED
        // (prevents i915/hyprland priority inversion, per GameMode's guidance)
        // Returns true on success, false otherwise
        static bool ApplySchedulingPolicy(pid_t pid, int policy);

        // Restore a process to SCHED_OTHER with nice 0
        static bool RestoreDefaultScheduling(pid_t pid);

        // How many logical CPUs this system has (via sysconf)
        static int GetLogicalCpuCount();
    };

}
