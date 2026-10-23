#include "SvgReader.hh"

#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "Exhaustive.hh"
#include "SvgSchema.hh"
#include "SvgValueReader.hh"

namespace {

struct SvgParseFailure {
    SvgDiagnostic diagnostic;
};

bool whitespace_only(std::string_view text) {
    for (const char character : text) {
        if (std::isspace(static_cast<unsigned char>(character)) == 0) return false;
    }
    return true;
}

bool namespace_declaration(std::string_view name) {
    return name == "xmlns" || name.starts_with("xmlns:");
}

enum class Content {
    None,
    Elements,
    TextAndElements,
};

Content content_of(svg_schema::ElementFamily family) {
    switch (family) {
        case svg_schema::ElementFamily::Shape: return Content::None;
        case svg_schema::ElementFamily::Container: return Content::Elements;
        case svg_schema::ElementFamily::Text: return Content::TextAndElements;
    }
    return Content::None;
}

// Dispatch by an SVG element name needs to ask every alternative, not only the active
// one a default-constructed variant would visit. This builds each alternative in turn, so
// adding a payload kind also instantiates its reader branch.
template<typename Variant, typename Action>
void for_each_alternative(Action&& action) {
    [&action]<std::size_t... Index>(std::index_sequence<Index...>) {
        (static_cast<void>(action(std::variant_alternative_t<Index, Variant>{})), ...);
    }(std::make_index_sequence<std::variant_size_v<Variant>>{});
}

class SvgReader {
public:
    explicit SvgReader(const XmlSnapshot& xml) : xml_(xml) {}

    ParsedSvg read() {
        const XmlNode& root = xml_.nodes[xml_.root];
        if (root.kind != XmlNodeKind::Element || Xml::local_name(root.name) != "svg") {
            throw SvgParseFailure{SvgDiagnostic{SvgProblem::RootIsNotSvg, root.line, root.name, {}, {}, {}}};
        }

        parsed_.root = read_element(xml_.root);
        assign_missing_ids();
        return std::move(parsed_);
    }

private:
    [[noreturn]] void fail(SvgProblem problem, const XmlNode& node, std::string attribute = {},
                           std::string value = {}, std::string detail = {}) const {
        throw SvgParseFailure{
            SvgDiagnostic{problem, node.line, std::string{Xml::local_name(node.name)}, std::move(attribute),
                          std::move(value), std::move(detail)}};
    }

    std::optional<std::reference_wrapper<const XmlAttribute>> exact_attribute(const XmlNode& node,
                                                                              std::string_view name) const {
        return Xml::attribute(node, name);
    }

    std::optional<std::reference_wrapper<const XmlAttribute>> href_attribute(const XmlNode& node) const {
        if (const auto href = exact_attribute(node, "href")) return href;
        return exact_attribute(node, "xlink:href");
    }

    std::optional<LengthPercentage> optional_length(const XmlNode& node, std::string_view name) const {
        const auto found = exact_attribute(node, name);
        if (!found) return std::nullopt;
        const XmlAttribute& attribute = found->get();
        const std::optional<LengthPercentage> value = SvgValueReader::length(attribute.value);
        if (!value) fail(SvgProblem::InvalidAttributeValue, node, std::string{name}, attribute.value, "length");
        return value;
    }

    LengthPercentage length_or(const XmlNode& node, std::string_view name, LengthPercentage fallback) const {
        const std::optional<LengthPercentage> value = optional_length(node, name);
        return value ? *value : fallback;
    }

    void require_nonnegative(const XmlNode& node, std::string_view name, LengthPercentage value) const {
        const float number = std::holds_alternative<Length>(value) ? std::get<Length>(value).value
                                                                   : std::get<Percentage>(value).value;
        if (number < 0.0F) fail(SvgProblem::InvalidAttributeValue, node, std::string{name}, to_text(value), "nonnegative");
    }

    std::optional<Paint> optional_paint(const XmlNode& node, std::string_view name) const {
        const auto found = exact_attribute(node, name);
        if (!found) return std::nullopt;
        const XmlAttribute& attribute = found->get();
        const std::optional<Paint> value = SvgValueReader::paint(attribute.value);
        if (!value) fail(SvgProblem::InvalidAttributeValue, node, std::string{name}, attribute.value, "paint");
        return value;
    }

    std::optional<Container::ViewBox> optional_view_box(const XmlNode& node) const {
        const auto found = exact_attribute(node, "viewBox");
        if (!found) return std::nullopt;
        const XmlAttribute& attribute = found->get();
        const std::optional<Container::ViewBox> value = SvgValueReader::view_box(attribute.value);
        if (!value) fail(SvgProblem::InvalidAttributeValue, node, "viewBox", attribute.value, "viewBox");
        return value;
    }

    std::optional<Text::Anchor> optional_text_anchor(const XmlNode& node) const {
        const auto found = exact_attribute(node, "text-anchor");
        if (!found) return std::nullopt;
        const XmlAttribute& attribute = found->get();
        const std::optional<Text::Anchor> value = SvgValueReader::text_anchor(attribute.value);
        if (!value) fail(SvgProblem::InvalidAttributeValue, node, "text-anchor", attribute.value, "text-anchor");
        return value;
    }

    std::string string_or_empty(const XmlNode& node, std::string_view name) const {
        const auto found = exact_attribute(node, name);
        return found ? found->get().value : std::string{};
    }

    void read_common(const XmlNode& xml_node, SVGNode& element) const {
        element.identity_attr.id = string_or_empty(xml_node, "id");
        element.identity_attr.class_ = string_or_empty(xml_node, "class");
        element.paint_attr.fill = optional_paint(xml_node, "fill");
        element.paint_attr.stroke = optional_paint(xml_node, "stroke");
        if (const auto width = exact_attribute(xml_node, "stroke-width")) {
            const std::optional<LengthPercentage> parsed = SvgValueReader::length(width->get().value);
            if (!parsed) {
                fail(SvgProblem::InvalidAttributeValue, xml_node, "stroke-width", width->get().value, "length");
            }
            require_nonnegative(xml_node, "stroke-width", *parsed);
            element.paint_attr.stroke_width = *parsed;
        }

        if (const auto transform_attribute = exact_attribute(xml_node, "transform")) {
            const std::optional<Transform> parsed = SvgValueReader::transform(transform_attribute->get().value);
            if (!parsed) {
                fail(SvgProblem::InvalidAttributeValue, xml_node, "transform", transform_attribute->get().value,
                     "transform");
            }
            element.transform_attr.transform = *parsed;
        }
    }

    Shape read_shape(const XmlNode& node, std::string_view element) const {
        const LengthPercentage zero = Length{0.0F, Length::Unit::UserUnit};
        std::optional<Shape> result;
        for_each_alternative<Shape::Kinds>(
            [&](const auto& alternative) {
                using T = std::remove_cvref_t<decltype(alternative)>;
                if (result || element_name(alternative) != element) return;
                if constexpr (std::is_same_v<T, Shape::Rect>) {
                    const LengthPercentage width = length_or(node, "width", zero);
                    const LengthPercentage height = length_or(node, "height", zero);
                    require_nonnegative(node, "width", width);
                    require_nonnegative(node, "height", height);
                    result =
                        Shape{Shape::Rect{length_or(node, "x", zero), length_or(node, "y", zero), width, height}};
                } else if constexpr (std::is_same_v<T, Shape::Circle>) {
                    const LengthPercentage radius = length_or(node, "r", zero);
                    require_nonnegative(node, "r", radius);
                    result =
                        Shape{Shape::Circle{length_or(node, "cx", zero), length_or(node, "cy", zero), radius}};
                } else if constexpr (std::is_same_v<T, Shape::Ellipse>) {
                    const LengthPercentage rx = length_or(node, "rx", zero);
                    const LengthPercentage ry = length_or(node, "ry", zero);
                    require_nonnegative(node, "rx", rx);
                    require_nonnegative(node, "ry", ry);
                    result = Shape{
                        Shape::Ellipse{length_or(node, "cx", zero), length_or(node, "cy", zero), rx, ry}};
                } else if constexpr (std::is_same_v<T, Shape::Line>) {
                    result = Shape{Shape::Line{length_or(node, "x1", zero), length_or(node, "y1", zero),
                                               length_or(node, "x2", zero), length_or(node, "y2", zero)}};
                } else {
                    static_assert(always_false<T>, "no SVG reader for this shape alternative");
                }
            });
        if (!result) fail(SvgProblem::UnsupportedElement, node, {}, {}, std::string{element});
        return std::move(*result);
    }

    Text read_text(const XmlNode& node, std::string_view element) const {
        Text text;
        text.child_attr = ChildAttr{};
        text.common.x = optional_length(node, "x");
        text.common.y = optional_length(node, "y");
        text.common.dx = optional_length(node, "dx");
        text.common.dy = optional_length(node, "dy");
        text.common.text_anchor = optional_text_anchor(node);
        text.common.font_size = optional_length(node, "font-size");
        if (text.common.font_size) require_nonnegative(node, "font-size", *text.common.font_size);

        std::optional<Text> result;
        for_each_alternative<Text::Kinds>(
            [&](const auto& alternative) {
                using T = std::remove_cvref_t<decltype(alternative)>;
                if (result || element_name(alternative) != element) return;
                if constexpr (std::is_same_v<T, Text::Wrapper>) {
                    text.specific = Text::Wrapper{};
                } else if constexpr (std::is_same_v<T, Text::Span>) {
                    text.specific = Text::Span{};
                } else if constexpr (std::is_same_v<T, Text::Path>) {
                    const auto href = href_attribute(node);
                    text.specific =
                        Text::Path{optional_length(node, "startOffset"), href ? href->get().value : std::string{}};
                } else {
                    static_assert(always_false<T>, "no SVG reader for this text alternative");
                }
                result = std::move(text);
            });
        if (!result) fail(SvgProblem::UnsupportedElement, node, {}, {}, std::string{element});
        return std::move(*result);
    }

    Container read_container(const XmlNode& node, std::string_view element) const {
        Container container;
        container.child_attr = ChildAttr{};
        std::optional<Container> result;
        for_each_alternative<Container::Kinds>(
            [&](const auto& alternative) {
                using T = std::remove_cvref_t<decltype(alternative)>;
                if (result || element_name(alternative) != element) return;
                if constexpr (std::is_same_v<T, Container::Group>) {
                    container.specific = Container::Group{string_or_empty(node, "data-kind")};
                } else if constexpr (std::is_same_v<T, Container::Defs>) {
                    container.specific = Container::Defs{};
                } else if constexpr (std::is_same_v<T, Container::Symbol>) {
                    container.specific = Container::Symbol{};
                } else if constexpr (std::is_same_v<T, Container::Switch>) {
                    container.specific = Container::Switch{};
                } else if constexpr (std::is_same_v<T, Container::SVG>) {
                    const std::optional<LengthPercentage> width = optional_length(node, "width");
                    const std::optional<LengthPercentage> height = optional_length(node, "height");
                    if (width) require_nonnegative(node, "width", *width);
                    if (height) require_nonnegative(node, "height", *height);
                    Container::SVG canvas{width, height};
                    canvas.xmlns = string_or_empty(node, "xmlns");
                    canvas.view_box = optional_view_box(node);
                    container.specific = std::move(canvas);
                } else {
                    static_assert(always_false<T>, "no SVG reader for this container alternative");
                }
                result = std::move(container);
            });
        if (!result) fail(SvgProblem::UnsupportedElement, node, {}, {}, std::string{element});
        return std::move(*result);
    }

    void remember_id(const XmlNode& node, const std::string& id) {
        if (id.empty()) return;
        if (!valid_svg_id(id)) fail(SvgProblem::InvalidId, node, "id", id);
        // The root alias is not a kept id: assign_missing_ids replaces it, and two such
        // elements are not duplicates of each other because neither keeps the name.
        if (reserved_svg_id(id)) return;
        if (!explicit_ids_.insert(id).second) fail(SvgProblem::DuplicateId, node, "id", id);
    }

    std::size_t read_element(const std::size_t xml_index) {
        const XmlNode& xml_node = xml_.nodes[xml_index];
        const std::string_view element = Xml::local_name(xml_node.name);
        const svg_schema::ElementSchema* schema = svg_schema::schema_for(element);
        if (schema == nullptr) {
            fail(SvgProblem::UnsupportedElement, xml_node, {}, {}, std::string{element});
        }

        for (const XmlAttribute& attribute : xml_node.attributes) {
            if (namespace_declaration(attribute.name)) continue;
            if (!svg_schema::accepts_attribute(element, attribute.name)) {
                fail(SvgProblem::UnsupportedAttribute, xml_node, attribute.name, attribute.value);
            }
        }

        SVGNode payload;
        read_common(xml_node, payload);
        remember_id(xml_node, payload.identity_attr.id);

        switch (schema->family) {
            case svg_schema::ElementFamily::Shape: payload.data = read_shape(xml_node, element); break;
            case svg_schema::ElementFamily::Text: payload.data = read_text(xml_node, element); break;
            case svg_schema::ElementFamily::Container: payload.data = read_container(xml_node, element); break;
        }

        const std::size_t index = parsed_.nodes.size();
        parsed_.nodes.push_back(ParsedNode{Node{std::move(payload)}, std::string{element}, {}});

        const Content content = content_of(schema->family);
        for (const std::size_t child : xml_node.children) {
            const XmlNode& child_node = xml_.nodes[child];
            if (child_node.kind == XmlNodeKind::Text) {
                if (content == Content::TextAndElements) {
                    if (!child_node.text.empty()) {
                        const std::size_t text_index = parsed_.nodes.size();
                        parsed_.nodes.push_back(ParsedNode{Node{PlainText{child_node.text}}, "#text", {}});
                        parsed_.nodes[index].children.push_back(text_index);
                    }
                } else if (!whitespace_only(child_node.text)) {
                    fail(SvgProblem::UnexpectedText, child_node, {}, {}, "text outside a text element");
                }
                continue;
            }

            if (child_node.kind != XmlNodeKind::Element) continue;
            if (content == Content::None) {
                fail(SvgProblem::UnsupportedElement, child_node, {}, {}, std::string{Xml::local_name(child_node.name)});
            }
            if (content == Content::TextAndElements) {
                const std::string_view child_element = Xml::local_name(child_node.name);
                if (child_element != "tspan" && child_element != "textPath") {
                    fail(SvgProblem::UnsupportedElement, child_node, {}, {}, std::string{child_element});
                }
            }
            const std::size_t child_index = read_element(child);
            parsed_.nodes[index].children.push_back(child_index);
        }
        return index;
    }

    void assign_missing_ids() {
        std::unordered_set<std::string> used = explicit_ids_;
        std::unordered_map<std::string, std::size_t> next_numbers;
        for (std::size_t index = 0; index < parsed_.nodes.size(); ++index) {
            if (index == parsed_.root) continue;
            if (!std::holds_alternative<SVGNode>(parsed_.nodes[index].payload)) continue;

            SVGNode& element = std::get<SVGNode>(parsed_.nodes[index].payload);
            const bool unnamed = element.identity_attr.id.empty();
            if (!unnamed && !reserved_svg_id(element.identity_attr.id)) continue;
            std::string original_id = unnamed ? std::string{} : std::move(element.identity_attr.id);

            const std::string& kind = parsed_.nodes[index].element;
            std::size_t& next = next_numbers[kind];
            ++next;
            std::string generated = kind + std::to_string(next);
            while (used.contains(generated)) {
                ++next;
                generated = kind + std::to_string(next);
            }
            used.insert(generated);
            element.identity_attr.id = generated;
            parsed_.assigned_ids.push_back(AssignedId{generated, kind, std::move(original_id)});
        }
    }

    const XmlSnapshot& xml_;
    ParsedSvg parsed_;
    std::unordered_set<std::string> explicit_ids_;
};

}  // namespace

SvgReadResult read_svg(const XmlSnapshot& xml) {
    try {
        return SvgReader{xml}.read();
    } catch (const SvgParseFailure& failure) {
        return failure.diagnostic;
    }
}
