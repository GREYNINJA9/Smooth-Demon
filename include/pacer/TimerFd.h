#pragma once

#include <cstdint>

// RAII wrapper around a Linux CLOCK_MONOTONIC timerfd.
//
// Usage:
//   TimerFd timer;
//   timer.ArmAbsolute(wakeupPointNs);   // wakeupPointNs is CLOCK_MONOTONIC absolute time
//   timer.Wait();                       // blocks (via poll) until the timer fires
//
// The fd is non-blocking + close-on-exec. Wait() polls on it so a shutdown
// pipe (or any other fd) can be waited on alongside it if needed later.
class TimerFd
{
public:
    TimerFd();
    ~TimerFd();

    TimerFd( const TimerFd& ) = delete;
    TimerFd& operator=( const TimerFd& ) = delete;

    // Arms the timer to fire once at the given CLOCK_MONOTONIC absolute
    // timestamp (nanoseconds). Returns false on failure (check errno).
    bool ArmAbsolute( uint64_t monotonicNanos );

    // Blocks until the timer fires (or an error/shutdown occurs).
    // Returns true if the timer fired normally, false on error or if
    // Disarm()/shutdown caused an early, non-fired return.
    bool Wait();

    // Disarms the timer (it will not fire until re-armed) and, if a
    // thread is currently blocked in Wait(), wakes it up via the
    // internal shutdown pipe.
    void Disarm();

    int GetFd() const { return m_fd; }

private:
    int m_fd = -1;

    // Companion pipe so Wait() can be woken up immediately on shutdown
    // even while blocked in poll() with no timer expiry pending.
    int m_shutdownPipe[2] = { -1, -1 };
};
