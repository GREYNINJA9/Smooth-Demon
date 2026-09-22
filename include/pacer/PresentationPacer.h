#pragma once

#include <cstdint>

// Gamescope-style adaptive presentation scheduler.
//
// Tracks a rolling estimate of how long the previous frame's
// draw/compute work took, and uses it (plus a fixed safety redzone) to
// compute the CLOCK_MONOTONIC deadline at which a worker thread should
// wake up so its work finishes just before the next physical VBlank.
//
// Not thread-safe: intended to be owned and driven by a single
// presentation/worker thread. UpdateLastDrawDuration() and
// CalculateNextWakeup() are meant to be called from that same thread,
// once per frame, in that order (measure work, then feed it back and
// schedule the next wakeup for the frame after).
class PresentationPacer
{
public:
    static constexpr uint64_t kDefaultRedZoneNs = 1'650'000ULL;   // 1.65ms
    static constexpr uint64_t kStartingDrawTimeNs = 3'000'000ULL; // 3ms
    static constexpr uint64_t kDecayAlpha = 980;                  // 98%
    static constexpr uint64_t kDecayMax   = 1000;                 // 100%

    // refreshRateHz: physical display refresh rate, e.g. 60, 120, 144.
    explicit PresentationPacer( int refreshRateHz );

    // Feed back the measured duration (nanoseconds) of the most recently
    // completed frame's draw/compute work. Updates the rolling max/decay
    // filter used by CalculateNextWakeup().
    void UpdateLastDrawDuration( uint64_t drawTimeNs );

    // Given the current CLOCK_MONOTONIC time (nanoseconds), returns the
    // absolute CLOCK_MONOTONIC deadline (nanoseconds) at which the worker
    // should wake up to finish just before the next VBlank, accounting
    // for the rolling draw time estimate and the safety redzone.
    uint64_t CalculateNextWakeup( uint64_t currentMonotonicNs );

    // Tells the pacer which physical VBlank we actually landed on / are
    // targeting, so future wakeups are computed relative to it. Should be
    // called once per frame (e.g. right after CalculateNextWakeup, or when
    // an actual VBlank/page-flip event is observed) to keep the schedule
    // anchored to real display timing rather than drifting.
    void MarkVBlank( uint64_t vblankMonotonicNs );

    void SetRefreshRateHz( int refreshRateHz );
    void SetRedZoneNs( uint64_t redZoneNs ) { m_redZoneNs = redZoneNs; }

    uint64_t GetRollingDrawTimeNs() const { return m_rollingMaxDrawTimeNs; }
    uint64_t GetRedZoneNs() const { return m_redZoneNs; }
    uint64_t GetRefreshIntervalNs() const { return m_refreshIntervalNs; }
    uint64_t GetLastVBlankNs() const { return m_lastVBlankNs; }

private:
    uint64_t m_refreshIntervalNs;
    uint64_t m_lastVBlankNs = 0;
    uint64_t m_rollingMaxDrawTimeNs = kStartingDrawTimeNs;
    uint64_t m_redZoneNs = kDefaultRedZoneNs;
};
