#include "SvgWriter.hh"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ElementName.hh"

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

void append_attribute(std::string& out, std::string_view name, std::string_view value) {
    out += ' ';
    out += name;
    out += "=\"";
    out += escaped_attribute(value);
    out += '"';
}

struct GeometryAttributes {
    std::string operator()(const Shape::Rect& rect) const {
        return "x=\"" + escaped_attribute(to_text(rect.x)) + "\" y=\"" + escaped_attribute(to_text(rect.y)) +
               "\" width=\"" + escaped_attribute(to_text(rect.w)) + "\" height=\"" +
               escaped_attribute(to_text(rect.h)) + "\"";
    }
    std::string operator()(const Shape::Circle& circle) const {
        return "cx=\"" + escaped_attribute(to_text(circle.cx)) + "\" cy=\"" +
               escaped_attribute(to_text(circle.cy)) + "\" r=\"" + escaped_attribute(to_text(circle.r)) + "\"";
    }
    std::string operator()(const Shape::Ellipse& ellipse) const {
        return "cx=\"" + escaped_attribute(to_text(ellipse.cx)) + "\" cy=\"" +
               escaped_attribute(to_text(ellipse.cy)) + "\" rx=\"" + escaped_attribute(to_text(ellipse.rx)) +
               "\" ry=\"" + escaped_attribute(to_text(ellipse.ry)) + "\"";
    }
    std::string operator()(const Shape::Line& line) const {
        return "x1=\"" + escaped_attribute(to_text(line.x1)) + "\" y1=\"" +
               escaped_attribute(to_text(line.y1)) + "\" x2=\"" + escaped_attribute(to_text(line.x2)) +
               "\" y2=\"" + escaped_attribute(to_text(line.y2)) + "\"";
    }
};

std::string optional_length_attribute(std::string_view name, const std::optional<LengthPercentage>& value) {
    if (!value) return {};
    return std::string{name} + "=\"" + escaped_attribute(to_text(*value)) + "\"";
}

void append_data(std::string& attributes, const std::string& data) {
    if (data.empty()) return;
    if (!attributes.empty()) attributes += ' ';
    attributes += data;
}

struct ContainerAttributes {
    std::string operator()(const Container::SVG& canvas) const {
        std::string attributes;
        append_data(attributes, optional_length_attribute("width", canvas.width));
        append_data(attributes, optional_length_attribute("height", canvas.height));
        if (canvas.view_box) {
            append_data(attributes, "viewBox=\"" + escaped_attribute(to_text(*canvas.view_box)) + "\"");
        }
        return attributes;
    }
    std::string operator()(const Container::Group&) const { return {}; }
    std::string operator()(const Container::Defs&) const { return {}; }
    std::string operator()(const Container::Symbol&) const { return {}; }
    std::string operator()(const Container::Switch&) const { return {}; }
};

struct TextAttributes {
    std::string operator()(const Text& text) const {
        std::string attributes;
        append_data(attributes, optional_length_attribute("x", text.common.x));
        append_data(attributes, optional_length_attribute("y", text.common.y));
        append_data(attributes, optional_length_attribute("dx", text.common.dx));
        append_data(attributes, optional_length_attribute("dy", text.common.dy));
        if (text.common.text_anchor) {
            append_data(attributes,
                        "text-anchor=\"" + escaped_attribute(text_anchor_text(*text.common.text_anchor)) + "\"");
        }
        append_data(attributes, optional_length_attribute("font-size", text.common.font_size));
        if (const Text::Path* path = std::get_if<Text::Path>(&text.specific)) {
            append_data(attributes, optional_length_attribute("startOffset", path->startOffset));
            if (!path->href.empty()) {
                append_data(attributes, "href=\"" + escaped_attribute(path->href) + "\"");
            }
        }
        return attributes;
    }
};

struct DataAttributes {
    std::string operator()(const Shape& shape) const { return std::visit(GeometryAttributes{}, shape.specific); }
    std::string operator()(const Container& container) const {
        return std::visit(ContainerAttributes{}, container.specific);
    }
    std::string operator()(const Text& text) const { return TextAttributes{}(text); }
};

void indent(std::string& out, std::size_t depth) { out.append(depth * 2, ' '); }

bool has_text_child(const TreeStore& store, Id node) {
    for (const Id child : store.children(node)) {
        if (std::holds_alternative<PlainText>(store.record(child))) return true;
    }
    return false;
}

void write_node(const TreeStore& store, Id id, std::size_t depth, bool inline_node, std::string& out);

void write_element(const TreeStore& store, Id id, const SVGNode& element, std::size_t depth, bool inline_node,
                   std::string& out) {
    const std::string_view name = element_name(element);
    if (!inline_node) indent(out, depth);
    out += '<';
    out += name;
    if (name == "svg" && depth == 0) {
        const Container& container = std::get<Container>(element.data);
        const Container::SVG& canvas = std::get<Container::SVG>(container.specific);
        append_attribute(out, "xmlns", canvas.xmlns.empty() ? Container::SVG::default_xmlns : canvas.xmlns);
    }
    if (!element.identity_attr.id.empty()) append_attribute(out, "id", element.identity_attr.id);
    if (!element.identity_attr.class_.empty()) append_attribute(out, "class", element.identity_attr.class_);

    const std::string data = std::visit(DataAttributes{}, element.data);
    if (!data.empty()) {
        out += ' ';
        out += data;
    }
    if (element.paint_attr.fill) append_attribute(out, "fill", to_text(*element.paint_attr.fill));
    if (element.paint_attr.stroke) append_attribute(out, "stroke", to_text(*element.paint_attr.stroke));
    if (element.paint_attr.stroke_width) {
        append_attribute(out, "stroke-width", to_text(*element.paint_attr.stroke_width));
    }
    if (!is_identity(element.transform_attr.transform)) {
        append_attribute(out, "transform", transform_text(element.transform_attr.transform));
    }

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
    const Node& payload = store.record(id);
    if (const SVGNode* element = std::get_if<SVGNode>(&payload)) {
        write_element(store, id, *element, depth, inline_node, out);
        if (!inline_node) out += '\n';
        return;
    }
    out += escaped_text(std::get<PlainText>(payload).content);
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
