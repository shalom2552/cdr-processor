#include "doctest.h"
#include "util/signal_waiter.hpp"

#include <csignal>
#include <pthread.h>
#include <thread>
#include <type_traits>

namespace {

/* True when the signal is blocked on this thread right now */
bool blocked(int signum)
{
    sigset_t mask;
    sigemptyset(&mask);
    pthread_sigmask(SIG_BLOCK, nullptr, &mask);
    return sigismember(&mask, signum) == 1;
}

} // namespace

using namespace cdrp;

TEST_CASE("signal_waiter_is_neither_copyable_nor_copy_assignable")
{
    CHECK_FALSE(std::is_copy_constructible<SignalWaiter>::value);
    CHECK_FALSE(std::is_copy_assignable<SignalWaiter>::value);
}

TEST_CASE("signal_waiter_blocks_the_signals_that_stop_a_run")
{
    REQUIRE_FALSE(blocked(SIGINT));
    REQUIRE_FALSE(blocked(SIGTERM));

    const SignalWaiter signals;

    CHECK(blocked(SIGINT));
    CHECK(blocked(SIGTERM));
}

TEST_CASE("signal_waiter_puts_the_previous_mask_back")
{
    {
        const SignalWaiter signals;
        REQUIRE(blocked(SIGINT));
    }

    CHECK_FALSE(blocked(SIGINT));
    CHECK_FALSE(blocked(SIGTERM));
}

TEST_CASE("signal_waiter_returns_the_signal_that_arrived")
{
    const SignalWaiter signals;

    raise(SIGINT); // pending while blocked, so the wait below returns at once
    CHECK(signals.wait() == SIGINT);

    raise(SIGTERM);
    CHECK(signals.wait() == SIGTERM);
}

TEST_CASE("signal_waiter_blocks_the_threads_started_after_it")
{
    const SignalWaiter signals;
    bool inherited = false;

    std::thread worker([&] { inherited = blocked(SIGINT) && blocked(SIGTERM); });
    worker.join();

    CHECK(inherited);
}
