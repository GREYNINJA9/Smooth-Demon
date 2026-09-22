#include "resource/IOPriority.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <sys/syscall.h>
#include <unistd.h>

#ifndef IOPRIO_WHO_PROCESS
#define IOPRIO_WHO_PROCESS 1
#endif

namespace smoothdemon {

    static inline int sys_ioprio_set(int which, int who, int ioprio) {
        return static_cast<int>(::syscall(SYS_ioprio_set, which, who, ioprio));
    }

    static inline int sys_ioprio_get(int which, int who) {
        return static_cast<int>(::syscall(SYS_ioprio_get, which, who));
    }

    bool SetProcessIOPriority(pid_t pid, IOClass cls, int level) {
        if (level < 0) level = 0;
        if (level > 7) level = 7;

        const int value = MakeIOPrio(cls, level);
        if (sys_ioprio_set(IOPRIO_WHO_PROCESS, pid, value) != 0) {
            std::fprintf(stderr,
                        "[ioprio] set(pid=%d, cls=%d, lvl=%d) failed: %s\n",
                        pid, static_cast<int>(cls), level, std::strerror(errno));
            return false;
        }
        return true;
    }

    int GetProcessIOPriority(pid_t pid) {
        const int v = sys_ioprio_get(IOPRIO_WHO_PROCESS, pid);
        if (v < 0) return -1;
        return v & 0x1FFF;   // low 13 bits = priority data field
    }

}
