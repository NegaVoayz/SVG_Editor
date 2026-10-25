#include "SvgWriter.hh"

#include <stdexcept>
#include <string>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "ElementName.hh"
#include "Exhaustive.hh"
#include "SvgFields.hh"
#include "SvgDocument.hh"

namespace {

template<typename Value>
std::string serialize_attribute(const Value&) {
    static_assert(always_false<Value>, "no SVG writer for this attribute value type");
    return {};
}

template<>
std::string serialize_attribute<std::string>(const std::string& value) {
    return value;
}

template<>
std::string serialize_attribute<LengthPercentage>(const LengthPercentage& value) {
    return svg_text(value);
}

template<>
std::string serialize_attribute<MaybeLengthPercentage>(const MaybeLengthPercentage& value) {
    const std::optional<LengthPercentage> present = present_length_percentage(value);
    return present ? svg_text(*present) : std::string{};
}

template<>
std::string serialize_attribute<std::vector<Shape::Point>>(const std::vector<Shape::Point>& value) {
    return points_text(value, true);
}

template<>
std::string serialize_attribute<std::optional<Paint>>(const std::optional<Paint>& value) {
    return value ? to_text(*value) : std::string{};
}

template<>
std::string serialize_attribute<std::optional<Text::Anchor>>(const std::optional<Text::Anchor>& value) {
    return value ? std::string{text_anchor_text(*value)} : std::string{};
}

template<>
std::string serialize_attribute<std::optional<Container::ViewBox>>(
    const std::optional<Container::ViewBox>& value) {
    return value ? svg_text(*value) : std::string{};
}

template<>
std::string serialize_attribute<Transform>(const Transform& value) {
    return svg_text(value);
}

template<typename Owner, typename Value, std::size_t AliasCount>
void append_field(SvgNode& out, const Owner& owner,
                  const svg_fields::Field<Owner, Value, AliasCount>& field) {
    if (!field.options.serialize) return;
    std::string value = serialize_attribute(owner.*field.member);
    if (!value.empty()) out.add_attribute(std::string{field.name}, std::move(value));
}

template<typename Owner, typename Fields>
void append_fields(SvgNode& out, const Owner& owner, const Fields& fields) {
    std::apply(
        [&](const auto&... field) {
            (append_field(out, owner, field), ...);
        },
        fields);
}

struct ShapeAttributes {
    SvgNode& out;

    void operator()(const Shape::Rect& rect) const { append_fields(out, rect, svg_fields::rect_fields); }
    void operator()(const Shape::Circle& circle) const {
        append_fields(out, circle, svg_fields::circle_fields);
    }
    void operator()(const Shape::Ellipse& ellipse) const {
        append_fields(out, ellipse, svg_fields::ellipse_fields);
    }
    void operator()(const Shape::Line& line) const { append_fields(out, line, svg_fields::line_fields); }
    void operator()(const Shape::Polygon& polygon) const {
        append_fields(out, polygon, svg_fields::polygon_fields);
    }
    template<typename T>
    void operator()(const T&) const {
        static_assert(always_false<T>, "no serialization for this shape alternative");
    }
};

struct ContainerAttributes {
    SvgNode& out;

    void operator()(const Container::SVG& canvas) const { append_fields(out, canvas, svg_fields::svg_fields); }
    void operator()(const Container::Group& group) const {
        append_fields(out, group, svg_fields::group_fields);
    }
    void operator()(const Container::Defs&) const {}
    void operator()(const Container::Symbol&) const {}
    void operator()(const Container::Switch&) const {}
    template<typename T>
    void operator()(const T&) const {
        static_assert(always_false<T>, "no serialization for this container alternative");
    }
};

struct TextAttributes {
    SvgNode& out;

    void operator()(const Text& text) const {
        append_fields(out, text.common, svg_fields::text_common_fields);
        if (std::holds_alternative<Text::Path>(text.specific)) {
            const Text::Path& path = std::get<Text::Path>(text.specific);
            append_fields(out, path, svg_fields::text_path_fields);
        }
    }
};

struct DataAttributes {
    SvgNode& out;

    void operator()(const Shape& shape) const { std::visit(ShapeAttributes{out}, shape.specific); }
    void operator()(const Container& container) const {
        std::visit(ContainerAttributes{out}, container.specific);
    }
    void operator()(const Text& text) const { TextAttributes{out}(text); }
    template<typename T>
    void operator()(const T&) const {
        static_assert(always_false<T>, "no serialization for this element payload");
    }
};

SvgNode build_element(const Document& document, Id id, const SVGNode& element);

template<typename T>
SvgNode build_node_payload(const Document&, Id, const T&) {
    static_assert(always_false<T>, "no serialization for this node payload");
    return SvgNode::text("");
}

template<>
SvgNode build_node_payload<SVGNode>(const Document& document, Id id, const SVGNode& element) {
    return build_element(document, id, element);
}

template<>
SvgNode build_node_payload<PlainText>(const Document&, Id, const PlainText& text) {
    return SvgNode::text(text.content);
}

SvgNode build_node(const Document& document, Id id) {
    const std::optional<Node> payload = document.node(id);
    if (!payload) throw std::logic_error("write_svg: a child link names no live node");
    return std::visit(
        [&](const auto& payload) {
            using T = std::remove_cvref_t<decltype(payload)>;
            return build_node_payload<T>(document, id, payload);
        },
        *payload);
}

SvgNode build_element(const Document& document, Id id, const SVGNode& element) {
    const std::string_view name = element_name(element);
    SvgNode out = SvgNode::element(std::string{name});

    if (name == "svg" && id == document.root()) {
        const Container& container = std::get<Container>(element.data);
        const Container::SVG& canvas = std::get<Container::SVG>(container.specific);
        out.add_attribute(std::string{svg_fields::xmlns_field.name},
                          canvas.xmlns.empty() ? std::string{Container::SVG::default_xmlns} : canvas.xmlns);
    }

    append_fields(out, element.identity_attr, svg_fields::identity_fields);
    std::visit(DataAttributes{out}, element.data);
    append_fields(out, element.paint_attr, svg_fields::paint_fields);
    append_fields(out, element.transform_attr, svg_fields::transform_fields);

    for (const Id child : document.children(id)) out.add_child(build_node(document, child));
    return out;
}

}  // namespace

std::string write_svg(const Document& document) {
    if (!document.has_document()) throw std::logic_error("write_svg: the document has no root");
    const std::optional<Node> root_payload = document.node(document.root());
    if (!root_payload || !std::holds_alternative<SVGNode>(*root_payload)) {
        throw std::logic_error("write_svg: the document root is not an SVG element");
    }
    const SVGNode& root = std::get<SVGNode>(*root_payload);
    if (!std::holds_alternative<Container>(root.data) ||
        !std::holds_alternative<Container::SVG>(std::get<Container>(root.data).specific)) {
        throw std::logic_error("write_svg: the document root is not <svg>");
    }
    return file_system_detail::svg_document(build_element(document, document.root(), root));
}
