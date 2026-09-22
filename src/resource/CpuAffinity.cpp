#include "resource/CpuAffinity.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <sched.h>
#include <unistd.h>

namespace smoothdemon {

    bool SetThreadAffinity(pid_t tid, const std::vector<int>& cpuCores) {
        const long ncores = sysconf(_SC_NPROCESSORS_ONLN);
        const int  ncpu   = (ncores > 0) ? static_cast<int>(ncores) : 1;

        cpu_set_t set;
        CPU_ZERO(&set);

        int applied = 0;
        for (int core : cpuCores) {
            if (core < 0 || core >= ncpu) continue;   // silently filter
            CPU_SET(core, &set);
            ++applied;
        }

        if (applied == 0) {
            std::fprintf(stderr,
                "[affinity] no valid cores in request (system has %d)\n", ncpu);
            return false;
        }

        if (sched_setaffinity(tid, sizeof(set), &set) != 0) {
            std::fprintf(stderr,
                "[affinity] sched_setaffinity(tid=%d) failed: %s\n",
                tid, std::strerror(errno));
            return false;
        }
        return true;
    }

    std::vector<int> GetThreadAffinity(pid_t tid) {
        cpu_set_t set;
        CPU_ZERO(&set);
        if (sched_getaffinity(tid, sizeof(set), &set) != 0) return {};

        const long ncores = sysconf(_SC_NPROCESSORS_ONLN);
        const int  ncpu   = (ncores > 0) ? static_cast<int>(ncores) : 1;

        std::vector<int> out;
        for (int c = 0; c < ncpu; ++c) {
            if (CPU_ISSET(c, &set)) out.push_back(c);
        }
        return out;
    }

}
