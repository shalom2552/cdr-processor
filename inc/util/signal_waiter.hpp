#pragma once

#include <csignal>

namespace cdrp {

/**
 * Blocks the signals that stop a run, then waits for one of them.
 * Construct before the first thread starts: every thread started later inherits the
 * blocked mask, so the signal is left for wait() and reaches no one else.
 * The mask that was in place is put back at destruction.
 */
class SignalWaiter {
public:
    /* Constructor, blocks SIGINT and SIGTERM on this thread and the ones it starts later */
    SignalWaiter();

    ~SignalWaiter();

    SignalWaiter(const SignalWaiter&) = delete;
    SignalWaiter& operator=(const SignalWaiter&) = delete;

    /**
     * Waits until one of the blocked signals arrives, waiting again if the wait itself
     * was interrupted. It returns outside any handler, so the caller may log or lock.
     *
     * @return the number of the signal that arrived
     */
    int wait() const;

private:
    sigset_t m_mask;
    sigset_t m_previous;
};

} // namespace cdrp

