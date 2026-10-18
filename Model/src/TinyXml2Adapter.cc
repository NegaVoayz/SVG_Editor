#include "TinyXml2Adapter.hh"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif
#include <tinyxml2.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

// A deliberately generous but finite ceiling: XML nesting is a parser input, not a
// programming convention, and an unbounded recursive copy would let a malformed file
// exhaust the call stack.
constexpr std::size_t maximum_depth = 1024;
constexpr std::size_t no_index = static_cast<std::size_t>(-1);

std::size_t copy_node(const tinyxml2::XMLNode& source, XmlSnapshot& snapshot, std::size_t depth, bool& too_deep);

void copy_attributes(const tinyxml2::XMLElement& element, XmlNode& node) {
    for (const tinyxml2::XMLAttribute* attribute = element.FirstAttribute(); attribute != nullptr;
         attribute = attribute->Next()) {
        if (attribute->Name() == nullptr || attribute->Value() == nullptr) continue;
        node.attributes.push_back(XmlAttribute{attribute->Name(), attribute->Value()});
    }
}

std::size_t copy_text(const tinyxml2::XMLText& source, XmlSnapshot& snapshot) {
    const std::size_t index = snapshot.nodes.size();
    XmlNode node;
    node.kind = XmlNodeKind::Text;
    node.text = source.Value() == nullptr ? std::string{} : std::string{source.Value()};
    node.line = source.GetLineNum() < 0 ? 0 : static_cast<std::size_t>(source.GetLineNum());
    snapshot.nodes.push_back(std::move(node));
    return index;
}

std::size_t copy_element(const tinyxml2::XMLElement& source, XmlSnapshot& snapshot, std::size_t depth,
                         bool& too_deep) {
    const std::size_t index = snapshot.nodes.size();
    XmlNode node;
    node.kind = XmlNodeKind::Element;
    node.name = source.Value() == nullptr ? std::string{} : std::string{source.Value()};
    node.line = source.GetLineNum() < 0 ? 0 : static_cast<std::size_t>(source.GetLineNum());
    copy_attributes(source, node);
    snapshot.nodes.push_back(std::move(node));

    for (const tinyxml2::XMLNode* child = source.FirstChild(); child != nullptr; child = child->NextSibling()) {
        const std::size_t child_index = copy_node(*child, snapshot, depth + 1, too_deep);
        if (child_index != no_index) snapshot.nodes[index].children.push_back(child_index);
    }
    return index;
}

std::size_t copy_node(const tinyxml2::XMLNode& source, XmlSnapshot& snapshot, std::size_t depth, bool& too_deep) {
    if (depth > maximum_depth) {
        too_deep = true;
        return no_index;
    }
    if (const tinyxml2::XMLElement* element = source.ToElement(); element != nullptr) {
        return copy_element(*element, snapshot, depth, too_deep);
    }
    if (const tinyxml2::XMLText* text = source.ToText(); text != nullptr) {
        return copy_text(*text, snapshot);
    }
    // Declarations, comments and unknown XML syntax do not belong to the SVG tree.
    return no_index;
}

}  // namespace

XmlReadResult read_xml(std::string_view bytes) {
    bool has_content = false;
    for (const char character : bytes) {
        if (character != ' ' && character != '\t' && character != '\r' && character != '\n') {
            has_content = true;
            break;
        }
    }
    if (!has_content) return XmlReadError{XmlReadError::Kind::NoRoot, 1, "the XML has no root element"};

    tinyxml2::XMLDocument document;
    const tinyxml2::XMLError parsed = document.Parse(bytes.data(), bytes.size());
    if (parsed != tinyxml2::XML_SUCCESS) {
        return XmlReadError{XmlReadError::Kind::Malformed,
                            document.ErrorLineNum() < 0 ? 0 : static_cast<std::size_t>(document.ErrorLineNum()),
                            document.ErrorName() == nullptr ? std::string{} : std::string{document.ErrorName()}};
    }

    const tinyxml2::XMLElement* root = document.RootElement();
    if (root == nullptr) return XmlReadError{XmlReadError::Kind::NoRoot, 0, "the XML has no root element"};

    XmlSnapshot snapshot;
    bool too_deep = false;
    snapshot.root = copy_element(*root, snapshot, 0, too_deep);
    if (too_deep || snapshot.root == no_index) {
        return XmlReadError{XmlReadError::Kind::Malformed, 0, "the XML is nested too deeply"};
    }
    return snapshot;
}
