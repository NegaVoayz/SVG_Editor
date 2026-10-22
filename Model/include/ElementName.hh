#pragma once

#include <string_view>
#include <type_traits>
#include <variant>

#include "SVGNode.hh"

namespace element_name_detail {

// The exhaustive type-to-name table for nested payload variants. A missing
// specialization is a compile error when std::visit asks for it.
template <class>
inline constexpr bool unsupported = false;

template <class T>
constexpr std::string_view of() {
    static_assert(unsupported<T>, "no SVG element name for this payload type");
    return {};
}

template <> constexpr std::string_view of<Shape::Rect>() { return "rect"; }
template <> constexpr std::string_view of<Shape::Circle>() { return "circle"; }
template <> constexpr std::string_view of<Shape::Ellipse>() { return "ellipse"; }
template <> constexpr std::string_view of<Shape::Line>() { return "line"; }

template <> constexpr std::string_view of<Container::SVG>() { return "svg"; }
template <> constexpr std::string_view of<Container::Group>() { return "g"; }
template <> constexpr std::string_view of<Container::Defs>() { return "defs"; }
template <> constexpr std::string_view of<Container::Symbol>() { return "symbol"; }
template <> constexpr std::string_view of<Container::Switch>() { return "switch"; }

template <> constexpr std::string_view of<Text::Wrapper>() { return "text"; }
template <> constexpr std::string_view of<Text::Span>() { return "tspan"; }
template <> constexpr std::string_view of<Text::Path>() { return "textPath"; }

template <class Alternative>
constexpr std::string_view of(const Alternative&) {
    return of<std::remove_cvref_t<Alternative>>();
}

}  // namespace element_name_detail

inline std::string_view element_name(const Shape& shape) {
    return std::visit(
        [](const auto& specific) { return element_name_detail::of(specific); },
        shape.specific);
}

inline std::string_view element_name(const Container& container) {
    return std::visit(
        [](const auto& specific) { return element_name_detail::of(specific); },
        container.specific);
}

inline std::string_view element_name(const Text& text) {
    return std::visit(
        [](const auto& specific) { return element_name_detail::of(specific); },
        text.specific);
}

// The SVG element name a payload is written with. Character data has no element of its
// own, so it has no answer here; asking about one is not a question a caller has.
inline std::string_view element_name(const SVGNode& element) {
    return std::visit([](const auto& data) { return element_name(data); }, element.data);
}
