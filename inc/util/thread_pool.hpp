#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <vector>
#include <thread>

namespace cdrp {


class ThreadPool {
public:
    /**
     * Constructor, starts the worker threads and waits for tasks.
     *
     * @param workers_count: the number of worker threads
     * @param queue_size: the maximum number of queued tasks
     */
    ThreadPool(std::size_t workers_count, std::size_t queue_size);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * Queues a task, blocks while the queue is full.
     *
     * @param task: the work to run on a worker thread
     * @return true if queued, false once the pool is stopping
     */
    bool submit(std::function<void()> task);

private:
    /* Worker loop, runs tasks until the pool stops */
    void run();

private:
    std::mutex m_mutex;
    std::condition_variable m_cv_empty;
    std::condition_variable m_cv_full;
    std::queue<std::function<void()>> m_queue;
    std::size_t m_capacity;
    bool m_stop = false;
    std::vector<std::thread> m_workers; // constructed last, after all state is live
};

} // namespace cdrp

