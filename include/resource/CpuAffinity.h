#pragma once

#include <vector>

#include <sys/types.h>

namespace smoothdemon {

    // Pin a thread (tid) to a set of CPU cores
    // Invalid core indices are silently filtered
    // Returns true on success (at least one valid core applied)
    bool SetThreadAffinity(pid_t tid, const std::vector<int>& cpuCores);

    // Query the set of CPU cores the thread is currently allowed to run on
    std::vector<int> GetThreadAffinity(pid_t tid);

}
