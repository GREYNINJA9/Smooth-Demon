#include "pacer/PresentationPacer.h"

#include <algorithm>

namespace
{
    uint64_t RefreshHzToIntervalNs( int refreshRateHz )
    {
        if ( refreshRateHz <= 0 )
            refreshRateHz = 60;
        return 1'000'000'000ULL / static_cast<uint64_t>( refreshRateHz );
    }
}

PresentationPacer::PresentationPacer( int refreshRateHz )
    : m_refreshIntervalNs( RefreshHzToIntervalNs( refreshRateHz ) )
{
}

void PresentationPacer::SetRefreshRateHz( int refreshRateHz )
{
    m_refreshIntervalNs = RefreshHzToIntervalNs( refreshRateHz );
}

void PresentationPacer::UpdateLastDrawDuration( uint64_t drawTimeNs )
{
    if ( drawTimeNs > m_rollingMaxDrawTimeNs )
    {
        // Sawtooth: jump straight up to spikes so we don't miss the very
        // frame that caused them.
        m_rollingMaxDrawTimeNs = drawTimeNs;
    }
    else
    {
        // Geometric decay back down during stable rendering.
        m_rollingMaxDrawTimeNs =
            ( ( kDecayAlpha * m_rollingMaxDrawTimeNs ) + ( kDecayMax - kDecayAlpha ) * drawTimeNs ) / kDecayMax;
    }

    // Never let the estimate eat more than the interval minus redzone,
    // otherwise we'd have nowhere to schedule a wakeup.
    if ( m_redZoneNs < m_refreshIntervalNs )
        m_rollingMaxDrawTimeNs = std::min( m_rollingMaxDrawTimeNs, m_refreshIntervalNs - m_redZoneNs );
    else
        m_rollingMaxDrawTimeNs = 0;
}

uint64_t PresentationPacer::CalculateNextWakeup( uint64_t currentMonotonicNs )
{
    const uint64_t offsetNs = m_rollingMaxDrawTimeNs + m_redZoneNs;

    uint64_t targetVBlank = m_lastVBlankNs + m_refreshIntervalNs;

    // Guard against overruns: if work took longer than a whole refresh
    // interval (or we simply haven't seen a VBlank near "now" yet), walk
    // forward until the target is actually in the future relative to the
    // deadline we need to hit.
    while ( targetVBlank < currentMonotonicNs + offsetNs )
        targetVBlank += m_refreshIntervalNs;

    return targetVBlank - offsetNs;
}

void PresentationPacer::MarkVBlank( uint64_t vblankMonotonicNs )
{
    m_lastVBlankNs = vblankMonotonicNs;
}
