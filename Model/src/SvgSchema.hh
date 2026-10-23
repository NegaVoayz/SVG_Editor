#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "ElementName.hh"

// The one reader/writer attribute contract. The reader refuses anything this table does
// not name, and the writer validates every attribute against the same table before it can
// emit it. An attribute added to one side therefore cannot silently be omitted from the
// other.
namespace svg_schema {

enum class ElementFamily {
    Shape,
    Container,
    Text,
};

struct ElementSchema {
    std::string_view element;
    ElementFamily family;
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
    {"svg", ElementFamily::Container, svg_attributes},
    {"g", ElementFamily::Container, group_attributes},
    {"defs", ElementFamily::Container, no_attributes},
    {"symbol", ElementFamily::Container, no_attributes},
    {"switch", ElementFamily::Container, no_attributes},
    {"rect", ElementFamily::Shape, rect_attributes},
    {"circle", ElementFamily::Shape, circle_attributes},
    {"ellipse", ElementFamily::Shape, ellipse_attributes},
    {"line", ElementFamily::Shape, line_attributes},
    {"text", ElementFamily::Text, text_attributes},
    {"tspan", ElementFamily::Text, text_attributes},
    {"textPath", ElementFamily::Text, text_path_attributes},
};

inline constexpr const ElementSchema* schema_for(std::string_view element) {
    for (const ElementSchema& schema : elements) {
        if (schema.element == element) return &schema;
    }
    return nullptr;
}

inline constexpr bool is_element(std::string_view element) { return schema_for(element) != nullptr; }

// Every alternative of each payload variant must have exactly one schema row, and the
// row must put it in the family whose reader knows that type. The table size checks also
// reject an extra row, so the schema is the payload vocabulary rather than a hand-written
// subset of it.
template<typename Variant>
consteval bool has_schema_for_every_alternative() {
    return []<std::size_t... Index>(std::index_sequence<Index...>) {
        return (is_element(element_name(std::variant_alternative_t<Index, Variant>{})) && ...);
    }(std::make_index_sequence<std::variant_size_v<Variant>>{});
}

template<typename Variant>
consteval bool has_family_for_every_alternative(ElementFamily family) {
    return [family]<std::size_t... Index>(std::index_sequence<Index...>) {
        return ((schema_for(element_name(std::variant_alternative_t<Index, Variant>{}))->family == family) && ...);
    }(std::make_index_sequence<std::variant_size_v<Variant>>{});
}

inline constexpr std::size_t payload_element_count =
    std::variant_size_v<Shape::Kinds> + std::variant_size_v<Container::Kinds> + std::variant_size_v<Text::Kinds>;
static_assert(std::size(elements) == payload_element_count,
              "the SVG schema must have one row per element payload alternative");
static_assert(has_schema_for_every_alternative<Shape::Kinds>(),
              "the SVG schema is missing a shape alternative");
static_assert(has_schema_for_every_alternative<Container::Kinds>(),
              "the SVG schema is missing a container alternative");
static_assert(has_schema_for_every_alternative<Text::Kinds>(),
              "the SVG schema is missing a text alternative");
static_assert(has_family_for_every_alternative<Shape::Kinds>(ElementFamily::Shape),
              "a shape alternative has the wrong schema family");
static_assert(has_family_for_every_alternative<Container::Kinds>(ElementFamily::Container),
              "a container alternative has the wrong schema family");
static_assert(has_family_for_every_alternative<Text::Kinds>(ElementFamily::Text),
              "a text alternative has the wrong schema family");

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
