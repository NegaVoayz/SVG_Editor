#include "SvgWriter.hh"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ElementName.hh"
#include "Exhaustive.hh"
#include "SvgSchema.hh"

namespace {

std::string escaped_text(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char character : text) {
        switch (character) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '\r': escaped += "&#13;"; break;
            default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

std::string escaped_attribute(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char character : text) {
        switch (character) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\n': escaped += "&#10;"; break;
            case '\r': escaped += "&#13;"; break;
            case '\t': escaped += "&#9;"; break;
            default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

std::string attribute_text(std::string_view element, std::string_view name, std::string_view value) {
    if (!svg_schema::accepts_attribute(element, name)) {
        throw std::logic_error("write_svg: writer attribute " + std::string{name} + " is not accepted for <" +
                               std::string{element} + ">");
    }
    return std::string{name} + "=\"" + escaped_attribute(value) + "\"";
}

void append_attribute(std::string& out, std::string_view element, std::string_view name, std::string_view value) {
    out += ' ';
    out += attribute_text(element, name, value);
}

struct GeometryAttributes {
    std::string_view element;

    std::string operator()(const Shape::Rect& rect) const {
        return attribute_text(element, "x", svg_text(rect.x)) + " " +
               attribute_text(element, "y", svg_text(rect.y)) + " " +
               attribute_text(element, "width", svg_text(rect.w)) + " " +
               attribute_text(element, "height", svg_text(rect.h));
    }
    std::string operator()(const Shape::Circle& circle) const {
        return attribute_text(element, "cx", svg_text(circle.cx)) + " " +
               attribute_text(element, "cy", svg_text(circle.cy)) + " " +
               attribute_text(element, "r", svg_text(circle.r));
    }
    std::string operator()(const Shape::Ellipse& ellipse) const {
        return attribute_text(element, "cx", svg_text(ellipse.cx)) + " " +
               attribute_text(element, "cy", svg_text(ellipse.cy)) + " " +
               attribute_text(element, "rx", svg_text(ellipse.rx)) + " " +
               attribute_text(element, "ry", svg_text(ellipse.ry));
    }
    std::string operator()(const Shape::Line& line) const {
        return attribute_text(element, "x1", svg_text(line.x1)) + " " +
               attribute_text(element, "y1", svg_text(line.y1)) + " " +
               attribute_text(element, "x2", svg_text(line.x2)) + " " +
               attribute_text(element, "y2", svg_text(line.y2));
    }
    template <class T>
    std::string operator()(const T&) const {
        static_assert(always_false<T>, "no serialization for this shape alternative");
        return {};
    }
};

std::string optional_length_attribute(std::string_view element, std::string_view name,
                                      const std::optional<LengthPercentage>& value) {
    if (!value) return {};
    return attribute_text(element, name, svg_text(*value));
}

void append_data(std::string& attributes, const std::string& data) {
    if (data.empty()) return;
    if (!attributes.empty()) attributes += ' ';
    attributes += data;
}

struct ContainerAttributes {
    std::string_view element;

    std::string operator()(const Container::SVG& canvas) const {
        std::string attributes;
        append_data(attributes, optional_length_attribute(element, "width", canvas.width));
        append_data(attributes, optional_length_attribute(element, "height", canvas.height));
        if (canvas.view_box) {
            append_data(attributes, attribute_text(element, "viewBox", svg_text(*canvas.view_box)));
        }
        return attributes;
    }
    std::string operator()(const Container::Group&) const { return {}; }
    std::string operator()(const Container::Defs&) const { return {}; }
    std::string operator()(const Container::Symbol&) const { return {}; }
    std::string operator()(const Container::Switch&) const { return {}; }
    template <class T>
    std::string operator()(const T&) const {
        static_assert(always_false<T>, "no serialization for this container alternative");
        return {};
    }
};

struct TextAttributes {
    std::string_view element;

    std::string operator()(const Text& text) const {
        std::string attributes;
        append_data(attributes, optional_length_attribute(element, "x", text.common.x));
        append_data(attributes, optional_length_attribute(element, "y", text.common.y));
        append_data(attributes, optional_length_attribute(element, "dx", text.common.dx));
        append_data(attributes, optional_length_attribute(element, "dy", text.common.dy));
        if (text.common.text_anchor) {
            append_data(attributes,
                        attribute_text(element, "text-anchor", text_anchor_text(*text.common.text_anchor)));
        }
        append_data(attributes, optional_length_attribute(element, "font-size", text.common.font_size));
        if (const Text::Path* path = std::get_if<Text::Path>(&text.specific)) {
            append_data(attributes, optional_length_attribute(element, "startOffset", path->startOffset));
            if (!path->href.empty()) {
                append_data(attributes, attribute_text(element, "href", path->href));
            }
        }
        return attributes;
    }
};

struct DataAttributes {
    std::string_view element;

    std::string operator()(const Shape& shape) const {
        return std::visit(GeometryAttributes{element}, shape.specific);
    }
    std::string operator()(const Container& container) const {
        return std::visit(ContainerAttributes{element}, container.specific);
    }
    std::string operator()(const Text& text) const { return TextAttributes{element}(text); }
    template <class T>
    std::string operator()(const T&) const {
        static_assert(always_false<T>, "no serialization for this element payload");
        return {};
    }
};

void indent(std::string& out, std::size_t depth) { out.append(depth * 2, ' '); }

bool has_text_child(const TreeStore& store, Id node) {
    for (const Id child : store.children(node)) {
        if (std::holds_alternative<PlainText>(store.record(child))) return true;
    }
    return false;
}

void write_element(const TreeStore& store, Id id, const SVGNode& element, std::size_t depth, bool inline_node,
                   std::string& out);
void write_node(const TreeStore& store, Id id, std::size_t depth, bool inline_node, std::string& out);

struct WriteNodePayload {
    const TreeStore& store;
    Id id;
    std::size_t depth;
    bool inline_node;
    std::string& out;

    void operator()(const SVGNode& element) const {
        write_element(store, id, element, depth, inline_node, out);
        if (!inline_node) out += '\n';
    }
    void operator()(const PlainText& text) const { out += escaped_text(text.content); }
    template <class T>
    void operator()(const T&) const {
        static_assert(always_false<T>, "no serialization for this node payload");
    }
};

void write_element(const TreeStore& store, Id id, const SVGNode& element, std::size_t depth, bool inline_node,
                   std::string& out) {
    const std::string_view name = element_name(element);
    if (!inline_node) indent(out, depth);
    out += '<';
    out += name;
    if (name == "svg" && depth == 0) {
        const Container& container = std::get<Container>(element.data);
        const Container::SVG& canvas = std::get<Container::SVG>(container.specific);
        append_attribute(out, name, "xmlns",
                         canvas.xmlns.empty() ? Container::SVG::default_xmlns : canvas.xmlns);
    }
    if (!element.identity_attr.id.empty()) append_attribute(out, name, "id", element.identity_attr.id);
    if (!element.identity_attr.class_.empty()) append_attribute(out, name, "class", element.identity_attr.class_);

    const std::string data = std::visit(DataAttributes{name}, element.data);
    if (!data.empty()) {
        out += ' ';
        out += data;
    }
    if (element.paint_attr.fill) append_attribute(out, name, "fill", to_text(*element.paint_attr.fill));
    if (element.paint_attr.stroke) append_attribute(out, name, "stroke", to_text(*element.paint_attr.stroke));
    if (element.paint_attr.stroke_width) {
        append_attribute(out, name, "stroke-width", svg_text(*element.paint_attr.stroke_width));
    }
    const std::string transform = svg_text(element.transform_attr.transform);
    if (!transform.empty()) append_attribute(out, name, "transform", transform);

    const std::vector<Id> children = store.children(id);
    if (children.empty()) {
        out += "/>";
        return;
    }
    out += '>';

    const bool inline_children = has_text_child(store, id);
    if (!inline_children) out += '\n';
    for (const Id child : children) write_node(store, child, depth + 1, inline_children, out);
    if (!inline_children) indent(out, depth);
    out += "</";
    out += name;
    out += '>';
}

void write_node(const TreeStore& store, Id id, std::size_t depth, bool inline_node, std::string& out) {
    std::visit(WriteNodePayload{store, id, depth, inline_node, out}, store.record(id));
}

}  // namespace

std::string write_svg(const TreeStore& store) {
    if (store.root() == no_id) throw std::logic_error("write_svg: the document has no root");
    const Node& root_payload = store.record(store.root());
    if (!std::holds_alternative<SVGNode>(root_payload)) {
        throw std::logic_error("write_svg: the document root is not an SVG element");
    }
    const SVGNode& root = std::get<SVGNode>(root_payload);
    if (!std::holds_alternative<Container>(root.data) ||
        !std::holds_alternative<Container::SVG>(std::get<Container>(root.data).specific)) {
        throw std::logic_error("write_svg: the document root is not <svg>");
    }

    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    write_node(store, store.root(), 0, false, out);
    return out;
}
