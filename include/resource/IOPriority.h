#pragma once

#include "types.hpp"

#include <sys/types.h>

namespace smoothdemon {

    enum class IOClass : int {
        None       = 0,
        RealTime   = 1,
        BestEffort = 2,
        Idle       = 3,
    };

    // Build the IOPRIO value using the kernel 13-bit class shift
    inline int MakeIOPrio(IOClass cls, int prioData) noexcept {
        return (static_cast<int>(cls) << 13) | (prioData & 0x1FFF);
    }

    // Set I/O priority for one process/thread
    // level = 0 (highest BE priority) .. 7 (lowest BE priority)
    bool SetProcessIOPriority(pid_t pid, IOClass cls, int level);

    // Read current I/O priority level (BE class data field) Returns -1 on error
    int GetProcessIOPriority(pid_t pid);

}
