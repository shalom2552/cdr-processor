#include "source/dir_watcher.hpp"
#include "constants.hpp"
#include "logger.hpp"
#include "util/fs.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <system_error>
#include <filesystem>
#include <limits.h>
#include <unistd.h>
#include <string>

namespace fs = std::filesystem;

// Room for a burst of events in one read().
constexpr std::size_t EVENT_BUF_SIZE = 64 * (sizeof(inotify_event) + NAME_MAX + 1);

namespace cdrp {

DirWatcher::DirWatcher(const std::string& source_dir, const std::string& target_dir)
    : m_source_dir(source_dir)
    , m_target_dir(target_dir)
{
    if (!ensure_dir(m_source_dir) || !ensure_dir(m_target_dir)) {
        return;
    }

    m_fd = inotify_init1(IN_CLOEXEC);
    if (m_fd < 0) {
        logError("DirWatcher", "inotify_init failed: " + std::string(std::strerror(errno)));
        return;
    }
    m_wd = inotify_add_watch(m_fd, m_source_dir.c_str(), IN_MOVED_TO);
    if (m_wd < 0) {
        logError("DirWatcher", "cannot watch: " + m_source_dir + ": " + std::strerror(errno));
        close(m_fd);
        m_fd = -1;
        return;
    }
    m_event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (m_event_fd < 0) {
        logError("DirWatcher", "eventfd failed: " + std::string(std::strerror(errno)));
        close(m_fd);
        m_fd = -1;
        return;
    }
    logInfo("DirWatcher", "watching: " + m_source_dir);
    sweep(m_target_dir, true);  // crash recovery: already claimed
    sweep(m_source_dir, false); // delivered before startup
}

DirWatcher::~DirWatcher()
{
    if (m_fd >= 0) {
        inotify_rm_watch(m_fd, m_wd);
        close(m_fd);
    }
    if (m_event_fd >= 0) {
        close(m_event_fd);
    }
}

bool DirWatcher::ok() const
{
    return m_fd >= 0;
}

bool DirWatcher::next_file(std::string& path)
{
    if (m_fd < 0) {
        return false;
    }

    // wait for events to arrive
    while (m_queue.empty()) {
        if (!read_events()) {
            return false;
        }
    }

    path = m_queue.front(); m_queue.pop_front();
    logDebug("DirWatcher", "next_file: " + path);
    return true;
}

void DirWatcher::wake()
{
    if (m_event_fd < 0) {
        return;
    }
    uint64_t one = 1;
    if (write(m_event_fd, &one, sizeof(one)) != sizeof(one)) {
        logWarn("DirWatcher", "wake failed: " + std::string(std::strerror(errno)));
    }
}

bool DirWatcher::claim(const std::string& file_name)
{
    std::string source = (fs::path(m_source_dir) / file_name).string();
    std::string dest = (fs::path(m_target_dir) / file_name).string();
    if (std::rename(source.c_str(), dest.c_str()) != 0) {
        // ENOENT means another worker claimed it first, which is expected.
        if (errno == ENOENT) {
            logDebug("DirWatcher", "already claimed: " + file_name);
        } else {
            logWarn("DirWatcher", "claim failed: " + file_name + ": " + std::strerror(errno));
        }
        return false;
    }
    m_queue.push_back(dest);
    logInfo("DirWatcher", "claim: " + file_name);
    if (m_queue.size() > kBacklogAlert) {
        logDebug("DirWatcher", "backlog of " + std::to_string(m_queue.size()) + " files");
    }
    return true;
}

void DirWatcher::sweep(const std::string& dir, bool claimed)
{
    std::error_code ec;
    const fs::directory_iterator entries(dir, ec);
    if (ec) {
        logError("DirWatcher", "scan failed: " + dir + ": " + ec.message());
        return;
    }

    for (const auto& entry : entries) {
        if (!entry.is_regular_file()) {
            continue;
        } else if (claimed) {
            m_queue.push_back(entry.path().string());
        } else {
            claim(entry.path().filename().string());
        }
    }
}

bool DirWatcher::read_events()
{
    struct pollfd fds[2];
    fds[0] = { m_fd, POLLIN, 0 };
    fds[1] = { m_event_fd, POLLIN, 0 };

    if (poll(fds, 2, -1) < 0) {
        if (errno == EINTR) {
            return true; // interrupted by a signal, retry
        }
        logError("DirWatcher", "poll failed: " + std::string(std::strerror(errno)));
        return false;
    }

    if (fds[1].revents & POLLIN) {
        uint64_t val;
        while (read(m_event_fd, &val, sizeof(val)) > 0) {} // drain the eventfd
        logInfo("DirWatcher", "woken: shutdown requested");
        return false;
    }

    if (!(fds[0].revents & POLLIN)) {
        return true; // spurious wakeup, retry
    }

    char buffer[EVENT_BUF_SIZE];
    ssize_t n = read(m_fd, buffer, sizeof(buffer));
    if (n <= 0) {
        logError("DirWatcher", "inotify read failed: " + std::string(std::strerror(errno)));
        return false;
    }

    for (char* p = buffer; p < buffer + n;) {
        auto* ev = reinterpret_cast<inotify_event*>(p);
        if (ev->len > 0 && !(ev->mask & IN_ISDIR)) {
            claim(ev->name);
        }
        p += sizeof(inotify_event) + ev->len;
    }
    return true;
}

} // namespace cdrp

