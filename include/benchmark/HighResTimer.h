#pragma once

#include "types.hpp"

#include <chrono>

namespace smoothdemon {

    // Lightweight monotonic timer with both absolute-elapsed and tick-delta modes
    // Wraps std::chrono::steady_clock (CLOCK_MONOTONIC on Linux) to avoid
    // wall-clock jumps from NTP/leap seconds
    class HighResTimer {
    public:
        using Clock = std::chrono::steady_clock;

        HighResTimer();

        // Reset both the start anchor and the tick anchor to "now"
        void Reset();

        // Total time since construction / last Reset()
        u64 ElapsedNs() const;
        f64 ElapsedUs() const;
        f64 ElapsedMs() const;
        f64 ElapsedSec() const;

        // Delta since the previous Tick*() call; advances the tick anchor
        u64 TickNs();
        f64 TickUs();
        f64 TickMs();
        f64 TickSec();

    private:
        Clock::time_point start_;
        Clock::time_point last_tick_;
    };

}
