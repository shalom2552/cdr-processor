#pragma once

#include <string>

namespace cdrp {

/* Read only file mapping. */
class MappedFile {
public:
    /**
     * Constructor, maps the whole file read only, failures are logged not thrown.
     *
     * @param file_path: the file to map
     */
    explicit MappedFile(const std::string& file_path);
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    /* True if the file was mapped, an empty file counts as mapped */
    bool ok() const;

    /* True if the file holds no bytes */
    bool empty() const;

    /* Start of the mapping, nullptr if it failed or the file is empty */
    const char* data() const;

    /* Mapped size in bytes */
    std::size_t size() const;

private:
    const char* m_data = nullptr;
    std::size_t m_size = 0;
    bool m_ok = false;
};

} // namespace cdrp

