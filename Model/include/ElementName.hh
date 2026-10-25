#pragma once

#include <string_view>
#include <variant>

#include "Exhaustive.hh"
#include "SVGNode.hh"

// One compile-time mapping from an SVG payload type to its element name. The primary
// template is the single failure point: any type without an explicit specialization is an
// error, including a newly added alternative of a nested variant.
template <class T>
constexpr std::string_view element_name(const T&) {
    static_assert(always_false<T>, "no SVG element name for this payload type");
    return {};
}

template <> constexpr std::string_view element_name<Shape::Rect>(const Shape::Rect&) { return "rect"; }
template <> constexpr std::string_view element_name<Shape::Circle>(const Shape::Circle&) { return "circle"; }
template <> constexpr std::string_view element_name<Shape::Ellipse>(const Shape::Ellipse&) { return "ellipse"; }
template <> constexpr std::string_view element_name<Shape::Line>(const Shape::Line&) { return "line"; }
template <> constexpr std::string_view element_name<Shape::Polygon>(const Shape::Polygon&) { return "polygon"; }

template <> constexpr std::string_view element_name<Container::SVG>(const Container::SVG&) { return "svg"; }
template <> constexpr std::string_view element_name<Container::Group>(const Container::Group&) { return "g"; }
template <> constexpr std::string_view element_name<Container::Defs>(const Container::Defs&) { return "defs"; }
template <> constexpr std::string_view element_name<Container::Symbol>(const Container::Symbol&) { return "symbol"; }
template <> constexpr std::string_view element_name<Container::Switch>(const Container::Switch&) { return "switch"; }

template <> constexpr std::string_view element_name<Text::Wrapper>(const Text::Wrapper&) { return "text"; }
template <> constexpr std::string_view element_name<Text::Span>(const Text::Span&) { return "tspan"; }
template <> constexpr std::string_view element_name<Text::Path>(const Text::Path&) { return "textPath"; }

template <>
constexpr std::string_view element_name<Shape>(const Shape& shape) {
    return std::visit([](const auto& specific) { return element_name(specific); }, shape.specific);
}

template <>
constexpr std::string_view element_name<Container>(const Container& container) {
    return std::visit([](const auto& specific) { return element_name(specific); }, container.specific);
}

template <>
constexpr std::string_view element_name<Text>(const Text& text) {
    return std::visit([](const auto& specific) { return element_name(specific); }, text.specific);
}

// Character data is the other payload of Node and has no SVG element of its own. Passing
// it here reaches the primary template's static_assert rather than an overload failure.
template <>
constexpr std::string_view element_name<SVGNode>(const SVGNode& element) {
    return std::visit([](const auto& payload) { return element_name(payload); }, element.data);
}
