#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <string_view>

namespace cdrp {

/**
 * Builder of a flat JSON object.
 * Fields come out in the order they were added, quotes and backslashes escaped.
 * Hands back the finished text as a string.
 */
class Json {
public:
    /**
     * Adds a string field.
     *
     * @param name: the field name
     * @param value: the text to write, escaped
     * @return this object, so adds can be chained
     */
    Json& add(const std::string_view name, const std::string_view value);

    /**
     * Adds a number field.
     *
     * @param name: the field name
     * @param value: the number to write
     * @return this object, so adds can be chained
     */
    Json& add(const std::string_view name, uint64_t value);

    /**
     * Adds an array of strings, empty writes an empty array.
     *
     * @param name: the field name
     * @param values: the strings to write, each escaped
     * @return this object, so adds can be chained
     */
    Json& add(const std::string_view name, const std::vector<std::string>& values);

    /**
     * Builds the object text, the builder stays usable after it.
     *
     * @return the fields added so far, wrapped in braces
     */
    std::string str() const;

    /**
     * Builds a one field object holding an error message.
     *
     * @param msg: the message to write, escaped
     * @return the object text
     */
    static std::string error(std::string_view msg);

private:
    /* Writes the separator and the quoted field name */
    void key(std::string_view name);

    /* Writes text in quotes, escaping quotes, backslashes and control characters */
    void quoted(std::string_view text);

private:
    std::string m_body;
};

} // namespace cdrp

