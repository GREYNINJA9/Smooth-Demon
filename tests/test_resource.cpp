#include "resource/CpuAffinity.h"
#include "resource/IOPriority.h"
#include "resource/ProcessScheduler.h"

#include <cstdlib>
#include <iostream>
#include <string>

#include <sched.h>
#include <unistd.h>

static void print_section(const char* title) {
    std::cout << "\n---- " << title << " ----\n";
}

int main(int argc, char** argv) {
    using namespace smoothdemon;

    // Target self by default or argv[1] as PID
    const pid_t target = (argc > 1)
        ? static_cast<pid_t>(std::atoi(argv[1]))
        : getpid();

    std::cout << "=== SmoothDemon Resource Management Test ===\n";
    std::cout << "Logical CPUs: " << ProcessScheduler::GetLogicalCpuCount() << "\n";
    std::cout << "Target PID:   " << target << "\n";

    // ------------ Thread enumeration -----------------------------------------------------------------------
    print_section("Thread Discovery");
    const auto threads = ProcessScheduler::GetProcessThreads(target);
    std::cout << "Found " << threads.size() << " thread(s):\n";
    for (pid_t tid : threads) {
        std::cout << "  TID " << tid << "\n";
    }
    if (threads.empty()) {
        std::cerr << "FAIL: no threads enumerated\n";
        return 1;
    }

    // -------- Current nice -------------------------------------------------------------------------
    print_section("Current Nice");
    const int cur_nice = ProcessScheduler::GetAverageNice(target);
    std::cout << "Average nice: " << cur_nice << "\n";

    // -------- Renice to +5 safe and no privilege required ----------------------------------------------
    print_section("Renice +5");
    const bool nice_ok = ProcessScheduler::ReniceProcessThreads(target, 5);
    std::cout << "Renice to +5: " << (nice_ok ? "OK" : "FAILED") << "\n";
    if (nice_ok) {
        std::cout << "New average nice: "
                  << ProcessScheduler::GetAverageNice(target) << "\n";
    }

    // ------- Renice back to 0 -------------------------------------------------------------------------
    print_section("Renice 0");
    ProcessScheduler::ReniceProcessThreads(target, 0);
    std::cout << "Average nice restored to: "
              << ProcessScheduler::GetAverageNice(target) << "\n";

    // ------- Negative nice (requires CAP_SYS_NICE) ----------------------------------------------------------------
    print_section("Renice -10 (needs CAP_SYS_NICE)");
    const bool high_ok = ProcessScheduler::ReniceProcessThreads(target, -10);
    if (high_ok) {
        std::cout << "Elevated priority OK — average nice: "
                  << ProcessScheduler::GetAverageNice(target) << "\n";
        ProcessScheduler::ReniceProcessThreads(target, 0);  // restore
    } else {
        std::cout << "Elevated priority DENIED (expected without cap_sys_nice)\n";
    }

    // -------- I/O priority --------------------------------------------------------------------------------
    print_section("I/O Priority (BestEffort level 0)");
    const bool io_ok = SetProcessIOPriority(target, IOClass::BestEffort, 0);
    std::cout << "Set I/O prio: " << (io_ok ? "OK" : "FAILED") << "\n";
    const int io_read = GetProcessIOPriority(target);
    std::cout << "Read back I/O prio: " << io_read
              << " (expect >= 0 on success)\n";

    // -------- CPU affinity ------------------------------------------------------------------------------
    print_section("CPU Affinity");
    const auto before = GetThreadAffinity(target);
    std::cout << "Before: cores [";
    for (std::size_t i = 0; i < before.size(); ++i)
        std::cout << before[i] << (i + 1 < before.size() ? "," : "");
    std::cout << "]\n";

    const bool pin_ok = SetThreadAffinity(target, {0});
    std::cout << "Pin to core 0: " << (pin_ok ? "OK" : "FAILED") << "\n";

    const auto after = GetThreadAffinity(target);
    std::cout << "After:  cores [";
    for (std::size_t i = 0; i < after.size(); ++i)
        std::cout << after[i] << (i + 1 < after.size() ? "," : "");
    std::cout << "]\n";

    if (pin_ok && (after.size() != 1 || after[0] != 0)) {
        std::cerr << "FAIL: affinity pin did not take effect\n";
        return 2;
    }

    // ------ Dual-core real-time guard ------------------------------------------------------------------------------
    print_section("Dual-Core SCHED_FIFO Guard");
    const bool fifo_ok = ProcessScheduler::ApplySchedulingPolicy(target, SCHED_FIFO);
    const int cores = ProcessScheduler::GetLogicalCpuCount();
    if (cores <= 2) {
        std::cout << "Result: " << (fifo_ok ? "ALLOWED (unexpected!)" : "REFUSED")
                  << " — expected REFUSED on " << cores << "-core CPU\n";
        if (fifo_ok) {
            std::cerr << "FAIL: guard did not block SCHED_FIFO\n";
            return 3;
        }
    } else {
        std::cout << "Result: " << (fifo_ok ? "ALLOWED" : "REFUSED")
                  << " — n/a on " << cores << "-core CPU\n";
    }

    // -------- Safe policy (SCHED_BATCH is always allowed) ------------------------------------------------------------
    print_section("SCHED_BATCH (safe on any core count)");
    const bool batch_ok = ProcessScheduler::ApplySchedulingPolicy(target, SCHED_BATCH);
    std::cout << "SCHED_BATCH: " << (batch_ok ? "OK" : "FAILED") << "\n";

    // Restore normal scheduling
    ProcessScheduler::RestoreDefaultScheduling(target);

    std::cout << "\nAll resource management tests completed.\n";
    return 0;
}
