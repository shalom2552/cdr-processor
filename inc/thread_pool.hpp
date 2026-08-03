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
    /* creates a new thread pool with `workers` threads and `queue_size` tasks */
    ThreadPool(std::size_t workers_count, std::size_t queue_size);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /* blocks while the thread pool is busy */
    bool submit(std::function<void()> task);

private:
    /* workers loop */
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

