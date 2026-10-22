#pragma once

#include <string>
#include <string_view>

// A field is printed in quotes whenever it can contain a space; this keeps a name or a
// path from being read as several words of the answer, and lets it be pasted back into
// another instruction unchanged.
inline std::string escape_for_display(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char character : text) {
        if (character == '"' || character == '\\') escaped.push_back('\\');
        escaped.push_back(character);
    }
    return escaped;
}

inline std::string quote_if_needed(std::string_view text) {
    if (!text.empty() && text.find_first_of(" \t\r\n\"\\") == std::string_view::npos) {
        return std::string{text};
    }
    return "\"" + escape_for_display(text) + "\"";
}

// A reserved word is always quoted, even without a space: the quotes say it is a name,
// not part of the sentence around it.
inline std::string quoted(std::string_view text) { return "\"" + escape_for_display(text) + "\""; }
