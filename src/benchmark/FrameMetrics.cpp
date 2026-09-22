#include "benchmark/FrameMetrics.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace smoothdemon {

    FrameMetrics::FrameMetrics(std::size_t windowSize)
        : window_(std::min(windowSize, kMaxWindow)) {
        if (window_ == 0) window_ = 1;
    }

    void FrameMetrics::Reset() {
        frametimes_ms_.Clear();
        last_timestamp_ns_ = 0;
        have_last_ = false;
    }

    void FrameMetrics::RecordTimestamp(u64 timestampNs) {
        // First sample establishes baseline only
        if (!have_last_) {
            last_timestamp_ns_ = timestampNs;
            have_last_ = true;
            return;
        }

        // Guard against non-monotonic or duplicate timestamps
        if (timestampNs <= last_timestamp_ns_) return;

        const u64 deltaNs = timestampNs - last_timestamp_ns_;
        last_timestamp_ns_ = timestampNs;

        f64 deltaMs = static_cast<f64>(deltaNs) / 1'000'000.0;

        // Clamp extreme outliers (system sleep, process stall)
        if (deltaMs > max_frametime_ms_) deltaMs = max_frametime_ms_;

        frametimes_ms_.Push(deltaMs);
    }

    FrameStats FrameMetrics::ComputeStats() const {
        FrameStats out{};

        const std::size_t active = frametimes_ms_.Size();
        if (active == 0) return out;

        const std::size_t n = std::min(active, window_);

        // Copy only the most recent `n` samples into a sortable vector
        std::vector<f64> samples;
        samples.reserve(n);
        const std::size_t start = active - n;
        for (std::size_t i = start; i < active; ++i) {
            samples.push_back(frametimes_ms_[i]);
        }

        // Mean
        f64 sum = 0.0;
        for (f64 v : samples) sum += v;
        const f64 mean = sum / static_cast<f64>(n);

        // Min / max + variance + stutter count in one pass
        f64 mn = samples[0];
        f64 mx = samples[0];
        f64 sq = 0.0;
        u64 stutters = 0;
        const f64 stutter_threshold = 2.0 * mean;
        for (f64 v : samples) {
            if (v < mn) mn = v;
            if (v > mx) mx = v;
            const f64 d = v - mean;
            sq += d * d;
            if (v > stutter_threshold) ++stutters;
        }
        const f64 variance = sq / static_cast<f64>(n);
        const f64 stddev = std::sqrt(variance);

        // Percentiles: sort ascending, index with ceil(P/100 * n) capped at n-1
        std::sort(samples.begin(), samples.end());

        auto percentile = [&](f64 p) -> f64 {
            if (n == 0) return 0.0;
            std::size_t idx = static_cast<std::size_t>(std::ceil(p * static_cast<f64>(n)));
            if (idx >= n) idx = n - 1;
            return samples[idx];
        };

        const f64 p99  = percentile(0.99);
        const f64 p999 = percentile(0.999);

        out.avgFrametimeMs         = mean;
        out.avgFps                 = (mean > 0.0) ? 1000.0 / mean : 0.0;
        out.minFrametimeMs         = mn;
        out.maxFrametimeMs         = mx;
        out.p99FrametimeMs         = p99;
        out.p999FrametimeMs        = p999;
        out.onePercentLowFps       = (p99  > 0.0) ? 1000.0 / p99  : 0.0;
        out.pointOnePercentLowFps  = (p999 > 0.0) ? 1000.0 / p999 : 0.0;
        out.frametimeStdDevMs      = stddev;
        out.totalFrames            = static_cast<u64>(n);
        out.stutterCount           = stutters;

        return out;
    }

}
