#pragma once

#define CL_TARGET_OPENCL_VERSION 200

#include <CL/cl.h>

// Small header-only helpers for turning a profiling-enabled cl_event into
// human-readable/feedable timing numbers. Requires the command queue the
// event came from to have been created with CL_QUEUE_PROFILING_ENABLE
// (see OpenCLContext, which always does this).
namespace OpenCLProfiling
{
    // Returns the raw CL_PROFILING_COMMAND_START timestamp in nanoseconds
    // (device clock, not comparable to CLOCK_MONOTONIC). Returns 0 on
    // failure.
    inline cl_ulong GetEventStartNs( cl_event event )
    {
        cl_ulong start = 0;
        clGetEventProfilingInfo( event, CL_PROFILING_COMMAND_START, sizeof( start ), &start, nullptr );
        return start;
    }

    // Returns the raw CL_PROFILING_COMMAND_END timestamp in nanoseconds.
    // Returns 0 on failure.
    inline cl_ulong GetEventEndNs( cl_event event )
    {
        cl_ulong end = 0;
        clGetEventProfilingInfo( event, CL_PROFILING_COMMAND_END, sizeof( end ), &end, nullptr );
        return end;
    }

    // Duration between CL_PROFILING_COMMAND_START and
    // CL_PROFILING_COMMAND_END, in nanoseconds. This is the actual
    // hardware execution time of the kernel/command on the GPU, not
    // counting queue/submit latency. Feeds directly into Phase 10's
    // PresentationPacer::UpdateLastDrawDuration().
    inline uint64_t GetEventDurationNs( cl_event event )
    {
        const cl_ulong start = GetEventStartNs( event );
        const cl_ulong end = GetEventEndNs( event );
        return ( end > start ) ? static_cast<uint64_t>( end - start ) : 0;
    }

    // Convenience wrapper: duration in milliseconds as a double.
    inline double GetEventDurationMs( cl_event event )
    {
        return GetEventDurationNs( event ) / 1'000'000.0;
    }
}
