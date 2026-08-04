#include "util/mapped_file.hpp"
#include "logger.hpp"

#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cdrp {

MappedFile::MappedFile(const std::string& file_path)
{
    int fd = open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        logWarn("MappedFile", "open failed: " + file_path);
        return;
    }

    struct stat st{};
    if (fstat(fd, &st) != 0) {
        logWarn("MappedFile", "fstat failed: " + file_path);
    } else if (!S_ISREG(st.st_mode)) {
        logWarn("MappedFile", "not a regular file: " + file_path);
    } else if (st.st_size == 0) {
        logDebug("MappedFile", "empty file: " + file_path);
        m_ok = true; // empty file
    } else {
        void* p = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) {
            logWarn("MappedFile", "mmap failed: " + file_path);
        } else {
            m_data = static_cast<const char*>(p);
            m_size = static_cast<std::size_t>(st.st_size);
            m_ok = true;
            madvise(p, m_size, MADV_SEQUENTIAL);
        }
    }

    close(fd);
}

MappedFile::~MappedFile()
{
    if (m_data) {
        munmap(const_cast<char*>(m_data), m_size);
    }
}

bool MappedFile::ok() const
{
    return m_ok;
}

bool MappedFile::empty() const
{
    return m_size == 0;
}

const char* MappedFile::data() const
{
    return m_data;
}

std::size_t MappedFile::size() const
{
    return m_size;
}

} // namespace cdrp

