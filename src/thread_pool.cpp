#include "thread_pool.hpp"
#include "logger.hpp"

#include <cstddef>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace cdrp {

ThreadPool::ThreadPool(std::size_t workers_count, std::size_t capacity)
    : m_capacity(capacity)
{
    if (workers_count == 0 || capacity == 0) {
        throw std::invalid_argument("ThreadPool: workers and capacity must be > 0");
    }

    m_workers.reserve(workers_count);
    for (std::size_t i = 0; i < workers_count; ++i) {
        m_workers.emplace_back([this]() { run(); });
    }

    logInfo("ThreadPool", std::to_string(workers_count) + " workers, queue of "
        + std::to_string(capacity));
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv_empty.notify_all();
    m_cv_full.notify_all();
    for (auto& worker : m_workers) {
        worker.join();
    }
    logDebug("ThreadPool", "all workers joined");
}

bool ThreadPool::submit(std::function<void()> task)
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        // wait for space in the queue or stop signal
        m_cv_full.wait(lock, [this]() { return m_stop || m_queue.size() < m_capacity; });
        if (m_stop) {
            return false;
        }
        m_queue.push(std::move(task));
    }
    m_cv_empty.notify_one();
    return true;
}

void ThreadPool::run()
{
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            // wait for a task or stop signal
            m_cv_empty.wait(lock, [this]() { return m_stop || !m_queue.empty(); });
            if (m_stop && m_queue.empty()) { // only reachable if stoped and drained
                return;
            }
            task = std::move(m_queue.front());
            m_queue.pop();
        }
        m_cv_full.notify_one();

        try {
            task();
        } catch (const std::exception& e) {
            logError("ThreadPool", "task threw: " + std::string(e.what()));
        } catch (...) {
            logError("ThreadPool", "task threw an unknown exception");
        }
    }
}

} // namespace cdrp

