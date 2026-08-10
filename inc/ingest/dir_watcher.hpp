#pragma once

#include <string>
#include <deque>

namespace cdrp {

/**
 * Watches an input directory for CDR files delivered by rename.
 * Yields one claimed file path at a time.
 * Blocking; run on its own thread.
 */
class DirWatcher {
public:
    /**
     * Constructor, creates both directories if missing, starts the inotify watch
     * and sweeps files left from a previous run.
     *
     * @param source_dir: directory files are delivered to
     * @param target_dir: directory claimed files are moved to
     */
    DirWatcher(const std::string& source_dir, const std::string& target_dir);
    ~DirWatcher();

    DirWatcher(const DirWatcher&) = delete;
    DirWatcher& operator=(const DirWatcher&) = delete;

    /* True while the watcher is usable */
    bool ok() const;

    /**
     * Finds the next file to process.
     * Returns true if a new file was claimed, false on error.
     * Blocks until one is available, the watcher is closed, or wake() is called.
     *
     * @param path: set to the claimed file path, in target_dir
     * @return true if a file was claimed, false if woken, closed or failed
     */
    bool next_file(std::string& path);

    /**
     * Interrupts a blocked next_file() so it returns false.
     * Thread-safe; intended to be called from another thread to request shutdown.
     */
    void wake();

private:
    /* Atomically move a file to target_dir and add to the queue. */
    bool claim(const std::string& file_name);

    /* Sweep dir at startup and add to the queue, moves to target_dir if not claimed. */
    void sweep(const std::string& dir, bool claimed);

    /* Wait on the inotify and event fds, then drain any inotify events.
       Returns false if woken via wake() or on error. */
    bool read_events();

private:
    std::string m_source_dir;
    std::string m_target_dir;
    std::deque<std::string> m_queue;

    int m_fd = -1;
    int m_wd = -1;
    int m_event_fd = -1;
};

} // namespace cdrp

