#include "util/signal_waiter.hpp"

#include <csignal>
#include <pthread.h>

namespace cdrp {

SignalWaiter::SignalWaiter()
{
    sigemptyset(&m_mask);
    sigaddset(&m_mask, SIGINT);
    sigaddset(&m_mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &m_mask, &m_previous);
}

SignalWaiter::~SignalWaiter()
{
    pthread_sigmask(SIG_SETMASK, &m_previous, nullptr);
}

int SignalWaiter::wait() const
{
    int signum = 0;
    while (sigwait(&m_mask, &signum) != 0) {
        // interrupted, wait again
    }
    return signum;
}

} // namespace cdrp

