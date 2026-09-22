#include "benchmark/HighResTimer.h"

namespace smoothdemon {

    HighResTimer::HighResTimer()
        : start_(Clock::now()), last_tick_(start_) {}

    void HighResTimer::Reset() {
        const auto now = Clock::now();
        start_ = now;
        last_tick_ = now;
    }

    u64 HighResTimer::ElapsedNs() const {
        return static_cast<u64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - start_).count());
    }

    f64 HighResTimer::ElapsedUs() const {
        return std::chrono::duration<f64, std::micro>(Clock::now() - start_).count();
    }

    f64 HighResTimer::ElapsedMs() const {
        return std::chrono::duration<f64, std::milli>(Clock::now() - start_).count();
    }

    f64 HighResTimer::ElapsedSec() const {
        return std::chrono::duration<f64>(Clock::now() - start_).count();
    }

    u64 HighResTimer::TickNs() {
        const auto now = Clock::now();
        const auto d = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - last_tick_).count();
        last_tick_ = now;
        return static_cast<u64>(d);
    }

    f64 HighResTimer::TickUs() {
        const auto now = Clock::now();
        const f64 d = std::chrono::duration<f64, std::micro>(now - last_tick_).count();
        last_tick_ = now;
        return d;
    }

    f64 HighResTimer::TickMs() {
        const auto now = Clock::now();
        const f64 d = std::chrono::duration<f64, std::milli>(now - last_tick_).count();
        last_tick_ = now;
        return d;
    }

    f64 HighResTimer::TickSec() {
        const auto now = Clock::now();
        const f64 d = std::chrono::duration<f64>(now - last_tick_).count();
        last_tick_ = now;
        return d;
    }

}
