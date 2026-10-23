#pragma once

#include <array>
#include <span>
#include <string_view>

// The one reader/writer attribute contract. The reader refuses anything this table does
// not name, and the writer validates every attribute against the same table before it can
// emit it. An attribute added to one side therefore cannot silently be omitted from the
// other.
namespace svg_schema {

struct ElementSchema {
    std::string_view element;
    std::span<const std::string_view> attributes;
};

inline constexpr std::string_view common_attributes[] = {
    "id", "class", "fill", "stroke", "stroke-width", "transform",
};

inline constexpr std::string_view svg_attributes[] = {"width", "height", "viewBox", "xmlns"};
inline constexpr std::string_view group_attributes[] = {"data-kind"};
inline constexpr std::string_view rect_attributes[] = {"x", "y", "width", "height"};
inline constexpr std::string_view circle_attributes[] = {"cx", "cy", "r"};
inline constexpr std::string_view ellipse_attributes[] = {"cx", "cy", "rx", "ry"};
inline constexpr std::string_view line_attributes[] = {"x1", "y1", "x2", "y2"};
inline constexpr std::string_view text_attributes[] = {
    "x", "y", "dx", "dy", "text-anchor", "font-size",
};
inline constexpr std::string_view text_path_attributes[] = {
    "x", "y", "dx", "dy", "text-anchor", "font-size", "startOffset", "href", "xlink:href",
};
inline constexpr auto no_attributes = std::array<std::string_view, 0>{};

inline constexpr ElementSchema elements[] = {
    {"svg", svg_attributes},
    {"g", group_attributes},
    {"defs", no_attributes},
    {"symbol", no_attributes},
    {"switch", no_attributes},
    {"rect", rect_attributes},
    {"circle", circle_attributes},
    {"ellipse", ellipse_attributes},
    {"line", line_attributes},
    {"text", text_attributes},
    {"tspan", text_attributes},
    {"textPath", text_path_attributes},
};

inline const ElementSchema* schema_for(std::string_view element) {
    for (const ElementSchema& schema : elements) {
        if (schema.element == element) return &schema;
    }
    return nullptr;
}

inline bool is_element(std::string_view element) { return schema_for(element) != nullptr; }

inline bool accepts_attribute(std::string_view element, std::string_view attribute) {
    const ElementSchema* schema = schema_for(element);
    if (schema == nullptr) return false;
    for (const std::string_view name : common_attributes) {
        if (name == attribute) return true;
    }
    for (const std::string_view name : schema->attributes) {
        if (name == attribute) return true;
    }
    return false;
}

}  // namespace svg_schema
