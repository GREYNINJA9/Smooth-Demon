#include "benchmark/HighResTimer.h"
#include "benchmark/FrameMetrics.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

int main() {
    using namespace smoothdemon;

    std::cout << "=== SmoothDemon Benchmark & Frame Timing Test ===\n\n";

    // ------- Test 1: HighResTimer precision -----------------------------------------------------
    {
        HighResTimer timer;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const f64 elapsed = timer.ElapsedMs();
        std::cout << "[Timer] 20ms sleep measured: " << elapsed << " ms\n";
        if (elapsed < 15.0 || elapsed > 40.0) {
            std::cerr << "Timer out of plausible range\n";
            return 1;
        }
    }

    // --------- Test 2: Synthetic 60 FPS with periodic hitches -------------------------------------
    FrameMetrics metrics(1000);
    metrics.SetTargetFps(60.0);

    u64 simulatedTimeNs = 0;
    constexpr int kTotalFrames = 600;
    for (int i = 0; i < kTotalFrames; ++i) {
        u64 deltaNs = 16'666'666;      // ~16.67 ms to 60 FPS
        if (i % 100 == 0) {
            deltaNs = 50'000'000;      // 50 ms
        }
        simulatedTimeNs += deltaNs;
        metrics.RecordTimestamp(simulatedTimeNs);
    }

    const FrameStats s = metrics.ComputeStats();

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "\n[FrameMetrics] Synthetic " << kTotalFrames << "-frame run:\n";
    std::cout << "  Samples analyzed: " << s.totalFrames << "\n";
    std::cout << "  Avg frametime:    " << s.avgFrametimeMs << " ms\n";
    std::cout << "  Avg FPS:          " << s.avgFps << "\n";
    std::cout << "  Min frametime:    " << s.minFrametimeMs << " ms\n";
    std::cout << "  Max frametime:    " << s.maxFrametimeMs << " ms\n";
    std::cout << "  P99 frametime:    " << s.p99FrametimeMs << " ms\n";
    std::cout << "  P99.9 frametime:  " << s.p999FrametimeMs << " ms\n";
    std::cout << "  1% Low FPS:       " << s.onePercentLowFps << "\n";
    std::cout << "  0.1% Low FPS:     " << s.pointOnePercentLowFps << "\n";
    std::cout << "  Std Dev:          " << s.frametimeStdDevMs << " ms\n";
    std::cout << "  Stutter count:    " << s.stutterCount
              << " (frames > 2x avg)\n";

    // --------- Validation ------------------------------------------------------------
    if (s.totalFrames != 599) {                 // 600 iterations - 1 baseline
        std::cerr << "Unexpected frame count\n";
        return 2;
    }
    if (s.avgFps < 55.0 || s.avgFps > 61.0) {
        std::cerr << "Avg FPS out of expected range\n";
        return 3;
    }
    if (s.stutterCount != 5) {                  // 5 hitches (i=100..500)
        std::cerr << "Unexpected stutter count: " << s.stutterCount << "\n";
        return 4;
    }
    if (s.onePercentLowFps <= 0.0 || s.pointOnePercentLowFps <= 0.0) {
        std::cerr << "Percentile lows not computed\n";
        return 5;
    }

    // -------------- Test 3: Empty + edge cases ---------------------------------------------------
    {
        FrameMetrics empty(500);
        const FrameStats es = empty.ComputeStats();
        if (es.totalFrames != 0 || es.avgFps != 0.0) {
            std::cerr << "Empty metrics should return zero stats\n";
            return 6;
        }

        // Non-monotonic timestamp guard
        FrameMetrics edge(100);
        edge.RecordTimestamp(1'000'000);
        edge.RecordTimestamp(1'000'000);   // duplicate then ignored
        edge.RecordTimestamp(500'000);     // backwards then ignored
        if (edge.SampleCount() != 0) {
            std::cerr << "Edge case guard failed\n";
            return 7;
        }
    }

    std::cout << "\nAll benchmark engine tests passed.\n";
    return 0;
}
