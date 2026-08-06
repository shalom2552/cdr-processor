#include "doctest.h"
#include "util/thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using namespace std::chrono_literals;

/* Every wait in this file is bounded, so a broken pool fails instead of hanging */
constexpr auto kTimeout = 2s;

/* A gate the test opens once; workers park on it until then */
class Gate {
public:
    void open()
    {
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_open = true;
        }
        m_cv.notify_all();
    }

    /* Returns false if the gate was still shut when the timeout expired */
    bool wait()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, kTimeout, [this] { return m_open; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_open = false;
};

/* A counter the test can wait on reaching a target */
class Counter {
public:
    void increment()
    {
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            ++m_value;
        }
        m_cv.notify_all();
    }

    std::size_t value()
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_value;
    }

    /* Returns false if the counter was still below target when the timeout expired */
    bool waitFor(std::size_t target)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, kTimeout, [this, target] { return m_value >= target; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::size_t m_value = 0;
};

} // namespace

using namespace cdrp;

TEST_CASE("pool_is_neither_copyable_nor_copy_assignable")
{
    CHECK_FALSE(std::is_copy_constructible<ThreadPool>::value);
    CHECK_FALSE(std::is_copy_assignable<ThreadPool>::value);
}

TEST_CASE("pool_constructs_and_destructs_without_any_work")
{
    ThreadPool pool(4, 8);
}

TEST_CASE("pool_runs_a_submitted_task")
{
    Counter done;

    {
        ThreadPool pool(1, 1);
        CHECK(pool.submit([&done] { done.increment(); }));
        CHECK(done.waitFor(1));
    }

    CHECK(done.value() == 1);
}

TEST_CASE("pool_runs_every_task_exactly_once")
{
    constexpr std::size_t kTasks = 200;
    Counter done;

    {
        ThreadPool pool(4, 8);
        for (std::size_t task = 0; task < kTasks; ++task) {
            CHECK(pool.submit([&done] { done.increment(); }));
        }
        CHECK(done.waitFor(kTasks));
    }

    CHECK(done.value() == kTasks);
}

TEST_CASE("pool_runs_tasks_on_worker_threads_not_the_submitter")
{
    const std::thread::id submitter = std::this_thread::get_id();
    bool ranElsewhere = false;
    Counter done;

    {
        ThreadPool pool(1, 1);
        pool.submit([&] {
            ranElsewhere = std::this_thread::get_id() != submitter;
            done.increment();
        });
        CHECK(done.waitFor(1));
    }

    CHECK(ranElsewhere);
}

TEST_CASE("pool_runs_tasks_on_all_of_its_workers_at_once")
{
    constexpr std::size_t kWorkers = 4;
    Counter started;
    Gate gate;

    ThreadPool pool(kWorkers, kWorkers);
    for (std::size_t task = 0; task < kWorkers; ++task) {
        pool.submit([&] {
            started.increment();
            gate.wait();
        });
    }

    /* Only true if the workers run concurrently: a serial pool never reaches kWorkers */
    CHECK(started.waitFor(kWorkers));
    gate.open();
}

TEST_CASE("pool_blocks_submit_while_the_queue_is_full")
{
    constexpr std::size_t kCapacity = 2;
    Counter started;
    Gate gate;

    ThreadPool pool(1, kCapacity);

    /* Occupies the single worker, so later tasks can only pile up in the queue */
    pool.submit([&] {
        started.increment();
        gate.wait();
    });
    REQUIRE(started.waitFor(1));

    for (std::size_t task = 0; task < kCapacity; ++task) {
        CHECK(pool.submit([] {}));
    }

    std::atomic<bool> returned { false };
    std::thread blocked([&] {
        pool.submit([] {});
        returned = true;
    });

    std::this_thread::sleep_for(100ms);
    CHECK_FALSE(returned.load());

    gate.open();
    blocked.join();
    CHECK(returned.load());
}

TEST_CASE("pool_accepts_work_again_after_the_queue_drains")
{
    Counter done;
    ThreadPool pool(2, 2);

    for (int round = 0; round < 3; ++round) {
        CHECK(pool.submit([&done] { done.increment(); }));
        CHECK(pool.submit([&done] { done.increment(); }));
        REQUIRE(done.waitFor(static_cast<std::size_t>(round + 1) * 2));
    }

    CHECK(done.value() == 6);
}

TEST_CASE("pool_takes_queued_tasks_in_submission_order")
{
    constexpr std::size_t kTasks = 8;
    std::mutex mutex;
    std::vector<std::size_t> order;
    Counter done;

    ThreadPool pool(1, kTasks);
    for (std::size_t task = 0; task < kTasks; ++task) {
        pool.submit([&, task] {
            {
                const std::lock_guard<std::mutex> lock(mutex);
                order.push_back(task);
            }
            done.increment();
        });
    }
    REQUIRE(done.waitFor(kTasks));

    const std::lock_guard<std::mutex> lock(mutex);
    REQUIRE(order.size() == kTasks);
    for (std::size_t task = 0; task < kTasks; ++task) {
        CHECK(order[task] == task);
    }
}

TEST_CASE("pool_survives_many_producers_submitting_at_once")
{
    constexpr std::size_t kProducers = 4;
    constexpr std::size_t kPerProducer = 250;
    Counter done;

    {
        ThreadPool pool(4, 8);

        std::vector<std::thread> producers;
        for (std::size_t producer = 0; producer < kProducers; ++producer) {
            producers.emplace_back([&pool, &done] {
                for (std::size_t task = 0; task < kPerProducer; ++task) {
                    pool.submit([&done] { done.increment(); });
                }
            });
        }
        for (auto& producer : producers) {
            producer.join();
        }

        CHECK(done.waitFor(kProducers * kPerProducer));
    }

    CHECK(done.value() == kProducers * kPerProducer);
}

TEST_CASE("pool_destructor_waits_for_the_running_task")
{
    std::atomic<bool> finished { false };
    Counter started;

    {
        ThreadPool pool(1, 1);
        pool.submit([&] {
            started.increment();
            std::this_thread::sleep_for(100ms);
            finished = true;
        });
        REQUIRE(started.waitFor(1));
    }

    /* The destructor must join its workers, so no task outlives the pool */
    CHECK(finished.load());
}

TEST_CASE("pool_destructor_returns_while_the_queue_still_holds_tasks")
{
    Counter started;
    Gate gate;

    {
        ThreadPool pool(1, 8);
        pool.submit([&] {
            started.increment();
            gate.wait();
        });
        REQUIRE(started.waitFor(1));

        for (int task = 0; task < 8; ++task) {
            pool.submit([] {});
        }

        /* Frees the worker so the destructor below can finish, drained or dropped */
        gate.open();
    }

    CHECK(started.value() == 1);
}

TEST_CASE("pool_runs_a_task_that_submits_more_work")
{
    Counter done;

    {
        ThreadPool pool(2, 4);
        pool.submit([&] {
            pool.submit([&done] { done.increment(); });
            done.increment();
        });
        CHECK(done.waitFor(2));
    }

    CHECK(done.value() == 2);
}

TEST_CASE("pool_with_one_worker_and_one_slot_still_drains_a_long_stream")
{
    constexpr std::size_t kTasks = 100;
    Counter done;

    ThreadPool pool(1, 1);
    for (std::size_t task = 0; task < kTasks; ++task) {
        CHECK(pool.submit([&done] { done.increment(); }));
    }

    CHECK(done.waitFor(kTasks));
}
