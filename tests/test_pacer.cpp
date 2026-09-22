#include "pacer/PresentationPacer.h"
#include "pacer/TimerFd.h"
#include "benchmark/HighResTimer.h"

#include <iostream>
#include <thread>
#include <chrono>

int main()
{
    std::cout << "[SMOOTHBOOST] Testing Adaptive Presentation Pacer (Gamescope Model)..." << std::endl;

    PresentationPacer pacer( 60 ); // Target 60 Hz display
    TimerFd timer;
    smoothdemon::HighResTimer clock;

    // Anchor the pacer's notion of "last vblank" to now, so the very first
    // scheduled wakeup lands roughly one refresh interval out instead of
    // walking forward from time zero
    pacer.MarkVBlank( clock.ElapsedNs() );

    std::cout << "Simulating 120 frames with dynamic compute jitter..." << std::endl;

    for ( int frame = 0; frame < 120; ++frame )
    {
        uint64_t now = clock.ElapsedNs();

        // 1. Calculate predictive wakeup deadline
        uint64_t wakeupNs = pacer.CalculateNextWakeup( now );

        // 2. Arm timerfd and sleep until deadline
        timer.ArmAbsolute( wakeupNs );
        timer.Wait();

        uint64_t wokeAt = clock.ElapsedNs();

        // Simulate GPU Compute / Interpolation Workload
        uint64_t workStart = wokeAt;
        int simulatedWorkMs = ( frame % 30 == 0 ) ? 9 : 4;
        std::this_thread::sleep_for( std::chrono::milliseconds( simulatedWorkMs ) );
        uint64_t workEnd = clock.ElapsedNs();

        // Feed back measured execution time to update the rolling filter.
        pacer.UpdateLastDrawDuration( workEnd - workStart );

        // Advance the pacer's vblank anchor so the next wakeup is scheduled
        // one refresh interval on from where we actually woke up, keeping
        // the schedule locked to real wall-clock cadence rather than drifting
        pacer.MarkVBlank( wakeupNs );

        if ( frame % 30 == 0 )
        {
            std::cout << "Frame #" << frame
                      << " | Work: " << simulatedWorkMs << "ms"
                      << " | Rolling Draw Est: " << ( pacer.GetRollingDrawTimeNs() / 1'000'000.0 ) << "ms"
                      << " | RedZone: " << ( pacer.GetRedZoneNs() / 1'000'000.0 ) << "ms" << std::endl;
        }
    }

    std::cout << "Pacer simulation completed successfully." << std::endl;
    return 0;
}
