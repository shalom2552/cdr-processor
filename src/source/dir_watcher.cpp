#include "source/dir_watcher.hpp"
#include "logger.hpp"

#include <cerrno>
#include <cstring>
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
        logError("[DirWatcher] inotify_init failed: " + std::string(std::strerror(errno)));
        return;
    }
    m_wd = inotify_add_watch(m_fd, m_source_dir.c_str(), IN_MOVED_TO);
    if (m_wd < 0) {
        logError("[DirWatcher] cannot watch: " + m_source_dir + ": " + std::strerror(errno));
        close(m_fd);
        m_fd = -1;
        return;
    }
    logInfo("[DirWatcher] watching: " + m_source_dir);
    sweep(m_target_dir, true);  // crash recovery: already claimed
    sweep(m_source_dir, false); // delivered before startup
}

DirWatcher::~DirWatcher()
{
    if (m_fd >= 0) {
        inotify_rm_watch(m_fd, m_wd);
        close(m_fd);
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
    logInfo("[DirWatcher] next_file: " + path);
    return true;
}

bool DirWatcher::claim(const std::string& file_name)
{
    std::string source = (fs::path(m_source_dir) / file_name).string();
    std::string dest = (fs::path(m_target_dir) / file_name).string();
    if (std::rename(source.c_str(), dest.c_str()) != 0) {
        logWarn("[DirWatcher] skipping file it could not claim: " + file_name);
        return false;
    }
    m_queue.push_back(dest);
    logInfo("[DirWatcher] claim: " + file_name);
    return true;
}

bool DirWatcher::ensure_dir(const std::string& dir)
{
    std::error_code ec;
    if (fs::create_directories(dir, ec)) {
        logInfo("[DirWatcher] created: " + dir);
    } else if (ec || !fs::is_directory(dir, ec)) {
        logError("[DirWatcher] cannot create: " + dir + ": " + ec.message());
        return false;
    }
    return true;
}

void DirWatcher::sweep(const std::string& dir, bool claimed)
{
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        } else if (claimed) {
            m_queue.push_back(entry.path().string());
        } else {
            claim(entry.path().filename().string());
        }
    }
    if (ec) {
        logWarn("[DirWatcher] scan failed: " + dir);
    }
}

bool DirWatcher::read_events()
{
    char buffer[EVENT_BUF_SIZE];
    ssize_t n = read(m_fd, buffer, sizeof(buffer));

    if (n <= 0) {
        logError("[DirWatcher] inotify read failed");
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

