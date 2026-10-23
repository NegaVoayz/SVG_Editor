#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "SVGNode.hh"

// A field descriptor is the single connection between a C++ member and the SVG attribute
// that stores it. The same descriptor is used in both directions: by the reader to select
// a member from an XML attribute, by the writer to spell that member, and by the schema to
// decide whether the attribute is accepted for the element.
namespace svg_fields {

struct FieldOptions {
    bool nonnegative = false;
    // `xmlns` is emitted by the root writer, which supplies the SVG default when the
    // stored value is empty. It still participates in reading and schema validation.
    bool serialize = true;
};

template<typename Owner, typename Value, std::size_t AliasCount = 0>
struct Field {
    static constexpr std::size_t alias_count = AliasCount;

    std::string_view name;
    Value Owner::* member;
    std::array<std::string_view, AliasCount> aliases;
    FieldOptions options;
};

template<typename Owner, typename Value>
constexpr Field<Owner, Value, 0> field(std::string_view name, Value Owner::* member,
                                       FieldOptions options = {}) {
    return {name, member, {}, options};
}

template<typename Owner, typename Value, std::size_t AliasCount>
constexpr Field<Owner, Value, AliasCount> field(std::string_view name, Value Owner::* member,
                                                const std::array<std::string_view, AliasCount>& aliases,
                                                FieldOptions options = {}) {
    return {name, member, aliases, options};
}

inline constexpr auto xlink_href = std::array<std::string_view, 1>{"xlink:href"};

inline constexpr auto identity_fields = std::tuple{
    field("id", &IdentityAttr::id),
    field("class", &IdentityAttr::class_),
};

inline constexpr auto paint_fields = std::tuple{
    field("fill", &PaintAttr::fill),
    field("stroke", &PaintAttr::stroke),
    field("stroke-width", &PaintAttr::stroke_width, {.nonnegative = true}),
};

inline constexpr auto transform_fields = std::tuple{
    field("transform", &TransformAttr::transform),
};

inline constexpr auto common_fields = std::tuple_cat(identity_fields, paint_fields, transform_fields);

inline constexpr auto xmlns_field =
    field("xmlns", &Container::SVG::xmlns, {.serialize = false});

inline constexpr auto svg_fields = std::tuple{
    field("width", &Container::SVG::width, {.nonnegative = true}),
    field("height", &Container::SVG::height, {.nonnegative = true}),
    field("viewBox", &Container::SVG::view_box),
    xmlns_field,
};

inline constexpr auto group_fields = std::tuple{
    field("data-kind", &Container::Group::data_kind),
};

inline constexpr auto rect_fields = std::tuple{
    field("x", &Shape::Rect::x),
    field("y", &Shape::Rect::y),
    field("width", &Shape::Rect::w, {.nonnegative = true}),
    field("height", &Shape::Rect::h, {.nonnegative = true}),
};

inline constexpr auto circle_fields = std::tuple{
    field("cx", &Shape::Circle::cx),
    field("cy", &Shape::Circle::cy),
    field("r", &Shape::Circle::r, {.nonnegative = true}),
};

inline constexpr auto ellipse_fields = std::tuple{
    field("cx", &Shape::Ellipse::cx),
    field("cy", &Shape::Ellipse::cy),
    field("rx", &Shape::Ellipse::rx, {.nonnegative = true}),
    field("ry", &Shape::Ellipse::ry, {.nonnegative = true}),
};

inline constexpr auto line_fields = std::tuple{
    field("x1", &Shape::Line::x1),
    field("y1", &Shape::Line::y1),
    field("x2", &Shape::Line::x2),
    field("y2", &Shape::Line::y2),
};

inline constexpr auto text_common_fields = std::tuple{
    field("x", &Text::Common::x),
    field("y", &Text::Common::y),
    field("dx", &Text::Common::dx),
    field("dy", &Text::Common::dy),
    field("text-anchor", &Text::Common::text_anchor),
    field("font-size", &Text::Common::font_size, {.nonnegative = true}),
};

inline constexpr auto text_path_fields = std::tuple{
    field("startOffset", &Text::Path::startOffset),
    field("href", &Text::Path::href, xlink_href),
};

inline constexpr auto text_path_attributes = std::tuple_cat(text_common_fields, text_path_fields);

template<typename Fields>
std::vector<std::string_view> attribute_names(const Fields& fields) {
    std::vector<std::string_view> names;
    std::apply(
        [&](const auto&... field) {
            const auto append = [&](const auto& descriptor) {
                names.push_back(descriptor.name);
                names.insert(names.end(), descriptor.aliases.begin(), descriptor.aliases.end());
            };
            (append(field), ...);
        },
        fields);
    return names;
}

template<typename Fields>
bool has_unique_attribute_names(const Fields& fields) {
    const std::vector<std::string_view> names = attribute_names(fields);
    for (std::size_t left = 0; left < names.size(); ++left) {
        if (names[left].empty()) return false;
        for (std::size_t right = left + 1; right < names.size(); ++right) {
            if (names[left] == names[right]) return false;
        }
    }
    return true;
}

template<typename Left, typename Right>
bool disjoint_attribute_names(const Left& left, const Right& right) {
    const std::vector<std::string_view> left_names = attribute_names(left);
    const std::vector<std::string_view> right_names = attribute_names(right);
    for (const std::string_view a : left_names) {
        for (const std::string_view b : right_names) {
            if (a == b) return false;
        }
    }
    return true;
}

}  // namespace svg_fields
