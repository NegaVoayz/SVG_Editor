#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The pointer-free XML value tree the SVG reader consumes. tinyxml2 owns its DOM
// through raw links; this snapshot copies the useful parts into stable vectors and
// expresses relationships with indices, so no pointer escapes the adapter.
struct XmlAttribute {
    std::string name;
    std::string value;
};

enum class XmlNodeKind {
    Element,
    Text,
};

struct XmlNode {
    XmlNodeKind kind = XmlNodeKind::Element;
    std::string name;
    std::string text;
    std::size_t line = 0;
    std::vector<XmlAttribute> attributes;
    std::vector<std::size_t> children;
};

struct XmlSnapshot {
    std::vector<XmlNode> nodes;
    std::size_t root = 0;
};

struct XmlReadError {
    enum class Kind {
        Malformed,
        NoRoot,
    };

    Kind kind = Kind::Malformed;
    std::size_t line = 0;
    std::string detail;
};

using XmlReadResult = std::variant<XmlSnapshot, XmlReadError>;

namespace Xml {

std::string_view local_name(std::string_view qualified) noexcept;
std::optional<std::reference_wrapper<const XmlAttribute>> attribute(const XmlNode& node, std::string_view name) noexcept;

}  // namespace Xml
