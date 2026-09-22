#pragma once

#include "types.hpp"
#include "benchmark/RingBuffer.h"

#include <cstddef>

namespace smoothdemon {

    struct FrameStats {
        f64 avgFps{0.0};
        f64 avgFrametimeMs{0.0};
        f64 minFrametimeMs{0.0};
        f64 maxFrametimeMs{0.0};
        f64 p99FrametimeMs{0.0};        // 1% low boundary (frametime)
        f64 p999FrametimeMs{0.0};       // 0.1% low boundary (frametime)
        f64 onePercentLowFps{0.0};      // 1000 / p99
        f64 pointOnePercentLowFps{0.0}; // 1000 / p999
        f64 frametimeStdDevMs{0.0};
        u64 totalFrames{0};
        u64 stutterCount{0};            // frames > 2x avg frametime
    };

    class FrameMetrics {
    public:
        static constexpr std::size_t kMaxWindow = 4096;

        explicit FrameMetrics(std::size_t windowSize = 1000);

        void SetTargetFps(f64 fps) noexcept { target_fps_ = fps; }
        void SetMaxFrametimeMs(f64 ms) noexcept { max_frametime_ms_ = ms; }

        // Feed a monotonic timestamp in nanoseconds
        // First call establishes baseline; subsequent calls record deltas
        void RecordTimestamp(u64 timestampNs);

        FrameStats ComputeStats() const;

        void Reset();

        std::size_t SampleCount() const noexcept { return frametimes_ms_.Size(); }
        std::size_t WindowSize() const noexcept { return window_; }

    private:
        RingBuffer<f64, kMaxWindow> frametimes_ms_;
        std::size_t window_;
        u64 last_timestamp_ns_{0};
        bool have_last_{false};
        f64 target_fps_{60.0};
        f64 max_frametime_ms_{500.0};
    };

}
