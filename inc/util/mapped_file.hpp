#pragma once

#include <string>

namespace cdrp {

/* Read only file mapping. */
class MappedFile {
public:
    explicit MappedFile(const std::string& path);
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    bool ok() const;
    bool empty() const;

    const char* data() const;
    std::size_t size() const;

private:
    const char* m_data = nullptr;
    std::size_t m_size = 0;
    bool m_ok = false;
};

} // namespace cdrp

