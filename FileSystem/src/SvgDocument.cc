#include "SvgDocument.hh"

#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

SvgNode::SvgNode() = default;

SvgNode SvgNode::element(std::string name) {
    if (name.empty()) throw std::logic_error("SvgNode: an element needs a name");
    SvgNode node;
    node.name_ = std::move(name);
    return node;
}

SvgNode SvgNode::text(std::string content) {
    SvgNode node;
    node.is_text_ = true;
    node.content_ = std::move(content);
    return node;
}

bool SvgNode::is_text() const { return is_text_; }

const std::string& SvgNode::name() const { return name_; }

const std::string& SvgNode::content() const { return content_; }

const std::vector<SvgAttribute>& SvgNode::attributes() const { return attributes_; }

const std::vector<SvgNode>& SvgNode::children() const { return children_; }

void SvgNode::add_attribute(std::string name, std::string value) {
    if (is_text_) throw std::logic_error("SvgNode: character data cannot have attributes");
    attributes_.push_back(SvgAttribute{std::move(name), std::move(value)});
}

void SvgNode::add_child(SvgNode child) {
    if (is_text_) throw std::logic_error("SvgNode: character data cannot have children");
    children_.push_back(std::move(child));
}

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

void indent(std::string& out, std::size_t depth) { out.append(depth * 2, ' '); }

bool has_text_child(const SvgNode& node) {
    for (const SvgNode& child : node.children()) {
        if (child.is_text()) return true;
    }
    return false;
}

void append_element(const SvgNode& node, std::size_t depth, bool inline_node, std::string& out) {
    if (!inline_node) indent(out, depth);
    out += '<';
    out += node.name();
    for (const SvgAttribute& attribute : node.attributes()) {
        out += ' ';
        out += attribute.name;
        out += "=\"";
        out += escaped_attribute(attribute.value);
        out += '"';
    }

    if (node.children().empty()) {
        out += "/>";
        if (!inline_node) out += '\n';
        return;
    }
    out += '>';

    const bool inline_children = has_text_child(node);
    if (!inline_children) out += '\n';
    for (const SvgNode& child : node.children()) {
        if (child.is_text()) {
            out += escaped_text(child.content());
        } else {
            append_element(child, depth + 1, inline_children, out);
        }
    }
    if (!inline_children) indent(out, depth);
    out += "</";
    out += node.name();
    out += '>';
    if (!inline_node) out += '\n';
}

}  // namespace

namespace file_system_detail {

FileResult read(std::string_view path) {
    std::ifstream file{std::string(path), std::ios::binary};
    if (!file) return FileReadError{std::string(path)};

    std::ostringstream bytes;
    bytes << file.rdbuf();
    if (file.bad()) return FileReadError{std::string(path)};
    return FileBytes{bytes.str()};
}

FileResult write(std::string_view path, std::string_view text) {
    std::ofstream file{std::string(path), std::ios::binary | std::ios::trunc};
    if (!file) return FileWriteError{std::string(path)};

    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) return FileWriteError{std::string(path)};
    return FileWritten{};
}

std::string svg_document(const SvgNode& root) {
    if (root.is_text()) throw std::logic_error("svg_document: the root is character data");
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    append_element(root, 0, false, out);
    out.pop_back();
    return out;
}

}  // namespace file_system_detail
