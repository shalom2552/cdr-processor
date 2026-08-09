#include "query/json.hpp"

#include <string>

namespace cdrp {

Json& Json::add(const std::string_view name, const std::string_view value)
{
    key(name);
    quoted(value);
    return *this;
}

Json& Json::add(const std::string_view name, uint64_t value)
{
    key(name);
    m_body += std::to_string(value);
    return *this;
}

Json& Json::add(const std::string_view name, const std::vector<std::string>& values)
{
    key(name);
    m_body += '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            m_body += ',';
        }
        quoted(values[i]);
    }
    m_body += ']';
    return *this;
}

std::string Json::str() const
{
    return "{" + m_body + "}";
}

std::string Json::error(std::string_view msg)
{
    return Json().add("error", msg).str();
}

void Json::key(std::string_view name)
{
    if (!m_body.empty()) {
        m_body += ',';
    }
    quoted(name);
    m_body += ':';
}

void Json::quoted(std::string_view text)
{
    static const char kHex[] = "0123456789abcdef";

    m_body += '"';
    for (const char character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte == '"' || byte == '\\') {
            m_body += '\\';
            m_body += character;
        } else if (byte < 0x20) {
            m_body += "\\u00";
            m_body += kHex[byte >> 4];
            m_body += kHex[byte & 0x0f];
        } else {
            m_body += character;
        }
    }
    m_body += '"';
}

} // namespace cdrp

