#include "pacer/TimerFd.h"

#include <sys/timerfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <cerrno>
#include <iostream>

TimerFd::TimerFd()
{
    m_fd = timerfd_create( CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC );
    if ( m_fd < 0 )
    {
        std::cerr << "[TimerFd] timerfd_create failed: " << strerror( errno ) << std::endl;
        return;
    }

    if ( pipe2( m_shutdownPipe, O_CLOEXEC | O_NONBLOCK ) != 0 )
    {
        std::cerr << "[TimerFd] pipe2 failed: " << strerror( errno ) << std::endl;
    }
}

TimerFd::~TimerFd()
{
    Disarm();

    if ( m_fd >= 0 )
    {
        close( m_fd );
        m_fd = -1;
    }

    for ( int i = 0; i < 2; i++ )
    {
        if ( m_shutdownPipe[ i ] >= 0 )
        {
            close( m_shutdownPipe[ i ] );
            m_shutdownPipe[ i ] = -1;
        }
    }
}

bool TimerFd::ArmAbsolute( uint64_t monotonicNanos )
{
    if ( m_fd < 0 )
        return false;

    struct itimerspec ts{};
    ts.it_value.tv_sec  = static_cast<time_t>( monotonicNanos / 1'000'000'000ULL );
    ts.it_value.tv_nsec = static_cast<long>( monotonicNanos % 1'000'000'000ULL );
    // it_interval left zero-initialized -> one-shot timer.

    if ( timerfd_settime( m_fd, TFD_TIMER_ABSTIME, &ts, nullptr ) != 0 )
    {
        std::cerr << "[TimerFd] timerfd_settime failed: " << strerror( errno ) << std::endl;
        return false;
    }

    return true;
}

bool TimerFd::Wait()
{
    if ( m_fd < 0 )
        return false;

    struct pollfd fds[2] = {};
    fds[0].fd     = m_fd;
    fds[0].events = POLLIN;
    fds[1].fd     = m_shutdownPipe[ 0 ];
    fds[1].events = POLLIN;

    const int nFds = m_shutdownPipe[ 0 ] >= 0 ? 2 : 1;

    for ( ;; )
    {
        int ret = poll( fds, nFds, -1 );

        if ( ret < 0 )
        {
            if ( errno == EINTR )
                continue;
            std::cerr << "[TimerFd] poll failed: " << strerror( errno ) << std::endl;
            return false;
        }

        // Shutdown/disarm nudge takes priority: drain and bail without
        // touching the timer fd.
        if ( nFds == 2 && ( fds[1].revents & POLLIN ) )
        {
            uint8_t byte;
            while ( read( m_shutdownPipe[ 0 ], &byte, sizeof( byte ) ) > 0 )
                ;
            return false;
        }

        if ( fds[0].revents & POLLIN )
        {
            uint64_t expirations = 0;
            ssize_t n = read( m_fd, &expirations, sizeof( expirations ) );
            if ( n == sizeof( expirations ) )
                return true;

            if ( n < 0 && errno == EAGAIN )
                continue; // Spurious wakeup, keep polling.

            return false;
        }
    }
}

void TimerFd::Disarm()
{
    if ( m_fd >= 0 )
    {
        struct itimerspec ts{};
        timerfd_settime( m_fd, TFD_TIMER_ABSTIME, &ts, nullptr );
    }

    if ( m_shutdownPipe[ 1 ] >= 0 )
    {
        uint8_t byte = 0;
        // Best-effort nudge; ignore errors (e.g. pipe full is fine, it just
        // means a wakeup is already pending).
        ssize_t written = write( m_shutdownPipe[ 1 ], &byte, sizeof( byte ) );
        (void)written;
    }
}
