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

#include "ElementName.hh"
#include "Exhaustive.hh"
#include "SvgFields.hh"
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

class SvgReader;

template<typename T>
Shape::Kinds read_shape_payload(const SvgReader& reader, const XmlNode& node);

template<typename T>
Text::Kinds read_text_payload(const SvgReader& reader, const XmlNode& node);

template<typename T>
Container::Kinds read_container_payload(const SvgReader& reader, const XmlNode& node);

template<typename Value>
Value read_attribute_value(const SvgReader& reader, const XmlNode& node, const XmlAttribute& attribute,
                           bool nonnegative);

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
    template<typename T>
    friend Shape::Kinds read_shape_payload(const SvgReader& reader, const XmlNode& node);

    template<typename T>
    friend Text::Kinds read_text_payload(const SvgReader& reader, const XmlNode& node);

    template<typename T>
    friend Container::Kinds read_container_payload(const SvgReader& reader, const XmlNode& node);

    template<typename Value>
    friend Value read_attribute_value(const SvgReader& reader, const XmlNode& node,
                                      const XmlAttribute& attribute, bool nonnegative);

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

    void require_nonnegative(const XmlNode& node, std::string_view name, LengthPercentage value) const {
        const float number = std::holds_alternative<Length>(value) ? std::get<Length>(value).value
                                                                   : std::get<Percentage>(value).value;
        if (number < 0.0F) fail(SvgProblem::InvalidAttributeValue, node, std::string{name}, to_text(value), "nonnegative");
    }

    template<typename Owner, typename Value, std::size_t AliasCount>
    std::optional<std::reference_wrapper<const XmlAttribute>> attribute_for(
        const XmlNode& node, const svg_fields::Field<Owner, Value, AliasCount>& field) const {
        if (const auto found = exact_attribute(node, field.name)) return found;
        for (const std::string_view alias : field.aliases) {
            if (const auto found = exact_attribute(node, alias)) return found;
        }
        return std::nullopt;
    }

    template<typename Owner, typename Value, std::size_t AliasCount>
    void read_field(const XmlNode& node, Owner& owner,
                    const svg_fields::Field<Owner, Value, AliasCount>& field) const {
        const auto attribute = attribute_for(node, field);
        if (!attribute) return;
        owner.*field.member =
            read_attribute_value<Value>(*this, node, attribute->get(), field.options.nonnegative);
    }

    template<typename Owner, typename Fields>
    void read_fields(const XmlNode& node, Owner& owner, const Fields& fields) const {
        std::apply(
            [&](const auto&... field) {
                (read_field(node, owner, field), ...);
            },
            fields);
    }

    void read_common(const XmlNode& xml_node, SVGNode& element) const;

    Shape read_shape(const XmlNode& node, std::string_view element) const;
    Text read_text(const XmlNode& node, std::string_view element) const;
    Container read_container(const XmlNode& node, std::string_view element) const;

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

template<typename Value>
Value read_attribute_value(const SvgReader&, const XmlNode&, const XmlAttribute&, bool) {
    static_assert(always_false<Value>, "no SVG reader for this attribute value type");
    return Value{};
}

template<>
std::string read_attribute_value<std::string>(const SvgReader&, const XmlNode&, const XmlAttribute& attribute,
                                              bool) {
    return attribute.value;
}

template<>
LengthPercentage read_attribute_value<LengthPercentage>(const SvgReader& reader, const XmlNode& node,
                                                        const XmlAttribute& attribute, bool nonnegative) {
    const std::optional<LengthPercentage> value = SvgValueReader::length(attribute.value);
    if (!value) {
        reader.fail(SvgProblem::InvalidAttributeValue, node, attribute.name, attribute.value, "length");
    }
    if (nonnegative) reader.require_nonnegative(node, attribute.name, *value);
    return *value;
}

template<>
std::optional<LengthPercentage> read_attribute_value<std::optional<LengthPercentage>>(
    const SvgReader& reader, const XmlNode& node, const XmlAttribute& attribute, bool nonnegative) {
    return read_attribute_value<LengthPercentage>(reader, node, attribute, nonnegative);
}

template<>
std::optional<Paint> read_attribute_value<std::optional<Paint>>(const SvgReader& reader, const XmlNode& node,
                                                               const XmlAttribute& attribute, bool) {
    const std::optional<Paint> value = SvgValueReader::paint(attribute.value);
    if (!value) {
        reader.fail(SvgProblem::InvalidAttributeValue, node, attribute.name, attribute.value, "paint");
    }
    return value;
}

template<>
std::optional<Text::Anchor> read_attribute_value<std::optional<Text::Anchor>>(
    const SvgReader& reader, const XmlNode& node, const XmlAttribute& attribute, bool) {
    const std::optional<Text::Anchor> value = SvgValueReader::text_anchor(attribute.value);
    if (!value) {
        reader.fail(SvgProblem::InvalidAttributeValue, node, attribute.name, attribute.value, "text-anchor");
    }
    return value;
}

template<>
std::optional<Container::ViewBox> read_attribute_value<std::optional<Container::ViewBox>>(
    const SvgReader& reader, const XmlNode& node, const XmlAttribute& attribute, bool) {
    const std::optional<Container::ViewBox> value = SvgValueReader::view_box(attribute.value);
    if (!value) {
        reader.fail(SvgProblem::InvalidAttributeValue, node, attribute.name, attribute.value, "viewBox");
    }
    return value;
}

template<>
Transform read_attribute_value<Transform>(const SvgReader& reader, const XmlNode& node,
                                          const XmlAttribute& attribute, bool) {
    const std::optional<Transform> value = SvgValueReader::transform(attribute.value);
    if (!value) {
        reader.fail(SvgProblem::InvalidAttributeValue, node, attribute.name, attribute.value, "transform");
    }
    return *value;
}

void SvgReader::read_common(const XmlNode& node, SVGNode& element) const {
    read_fields(node, element.identity_attr, svg_fields::identity_fields);
    read_fields(node, element.paint_attr, svg_fields::paint_fields);
    read_fields(node, element.transform_attr, svg_fields::transform_fields);
}

// Element-name dispatch has to ask every alternative, not only the active one a
// default-constructed variant would visit. Each alternative enters its own explicit
// specialization; a new kind that has not been taught to the reader reaches the primary
// template and fails with the assertion below.
template<typename T>
Shape::Kinds read_shape_payload(const SvgReader&, const XmlNode&) {
    static_assert(always_false<T>, "no SVG reader for this shape alternative");
    return Shape::Rect{};
}

template<>
Shape::Kinds read_shape_payload<Shape::Rect>(const SvgReader& reader, const XmlNode& node) {
    const LengthPercentage zero = Length{0.0F, Length::Unit::UserUnit};
    Shape::Rect rect{zero, zero, zero, zero};
    reader.read_fields(node, rect, svg_fields::rect_fields);
    return rect;
}

template<>
Shape::Kinds read_shape_payload<Shape::Circle>(const SvgReader& reader, const XmlNode& node) {
    const LengthPercentage zero = Length{0.0F, Length::Unit::UserUnit};
    Shape::Circle circle{zero, zero, zero};
    reader.read_fields(node, circle, svg_fields::circle_fields);
    return circle;
}

template<>
Shape::Kinds read_shape_payload<Shape::Ellipse>(const SvgReader& reader, const XmlNode& node) {
    const LengthPercentage zero = Length{0.0F, Length::Unit::UserUnit};
    Shape::Ellipse ellipse{zero, zero, zero, zero};
    reader.read_fields(node, ellipse, svg_fields::ellipse_fields);
    return ellipse;
}

template<>
Shape::Kinds read_shape_payload<Shape::Line>(const SvgReader& reader, const XmlNode& node) {
    const LengthPercentage zero = Length{0.0F, Length::Unit::UserUnit};
    Shape::Line line{zero, zero, zero, zero};
    reader.read_fields(node, line, svg_fields::line_fields);
    return line;
}

template<typename T>
Text::Kinds read_text_payload(const SvgReader&, const XmlNode&) {
    static_assert(always_false<T>, "no SVG reader for this text alternative");
    return Text::Wrapper{};
}

template<>
Text::Kinds read_text_payload<Text::Wrapper>(const SvgReader&, const XmlNode&) {
    return Text::Wrapper{};
}

template<>
Text::Kinds read_text_payload<Text::Span>(const SvgReader&, const XmlNode&) {
    return Text::Span{};
}

template<>
Text::Kinds read_text_payload<Text::Path>(const SvgReader& reader, const XmlNode& node) {
    Text::Path path;
    reader.read_fields(node, path, svg_fields::text_path_fields);
    return path;
}

template<typename T>
Container::Kinds read_container_payload(const SvgReader&, const XmlNode&) {
    static_assert(always_false<T>, "no SVG reader for this container alternative");
    return Container::Group{};
}

template<>
Container::Kinds read_container_payload<Container::Group>(const SvgReader& reader, const XmlNode& node) {
    Container::Group group;
    reader.read_fields(node, group, svg_fields::group_fields);
    return group;
}

template<>
Container::Kinds read_container_payload<Container::Defs>(const SvgReader&, const XmlNode&) {
    return Container::Defs{};
}

template<>
Container::Kinds read_container_payload<Container::Symbol>(const SvgReader&, const XmlNode&) {
    return Container::Symbol{};
}

template<>
Container::Kinds read_container_payload<Container::Switch>(const SvgReader&, const XmlNode&) {
    return Container::Switch{};
}

template<>
Container::Kinds read_container_payload<Container::SVG>(const SvgReader& reader, const XmlNode& node) {
    Container::SVG canvas;
    reader.read_fields(node, canvas, svg_fields::svg_fields);
    return canvas;
}

Shape SvgReader::read_shape(const XmlNode& node, std::string_view element) const {
    std::optional<Shape> result;
    for_each_alternative<Shape::Kinds>([&](const auto& alternative) {
        using T = std::remove_cvref_t<decltype(alternative)>;
        if (result || element_name(alternative) != element) return;
        result = Shape{read_shape_payload<T>(*this, node)};
    });
    if (!result) fail(SvgProblem::UnsupportedElement, node, {}, {}, std::string{element});
    return std::move(*result);
}

Text SvgReader::read_text(const XmlNode& node, std::string_view element) const {
    Text text;
    text.child_attr = ChildAttr{};
    read_fields(node, text.common, svg_fields::text_common_fields);

    std::optional<Text::Kinds> specific;
    for_each_alternative<Text::Kinds>([&](const auto& alternative) {
        using T = std::remove_cvref_t<decltype(alternative)>;
        if (specific || element_name(alternative) != element) return;
        specific = read_text_payload<T>(*this, node);
    });
    if (!specific) fail(SvgProblem::UnsupportedElement, node, {}, {}, std::string{element});
    text.specific = std::move(*specific);
    return text;
}

Container SvgReader::read_container(const XmlNode& node, std::string_view element) const {
    std::optional<Container::Kinds> specific;
    for_each_alternative<Container::Kinds>([&](const auto& alternative) {
        using T = std::remove_cvref_t<decltype(alternative)>;
        if (specific || element_name(alternative) != element) return;
        specific = read_container_payload<T>(*this, node);
    });
    if (!specific) fail(SvgProblem::UnsupportedElement, node, {}, {}, std::string{element});
    Container container;
    container.child_attr = ChildAttr{};
    container.specific = std::move(*specific);
    return container;
}

}  // namespace

SvgReadResult read_svg(const XmlSnapshot& xml) {
    try {
        return SvgReader{xml}.read();
    } catch (const SvgParseFailure& failure) {
        return failure.diagnostic;
    }
}
