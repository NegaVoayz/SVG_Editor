// `set <id> <attribute> <value>`: the attribute is the one word of the front end that
// decides a value's syntax rather than its place in the line. One row per attribute, so a
// word is either in this table or refused by the one place that knows the table; the type
// in the row is what makes the line a typed intent without a switch over attribute names.
#include "Set.hh"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Quoting.hh"
#include "SvgValue.hh"

namespace {

// One attribute: the word that names it, and the reader that turns the value after it into
// the one instruction that writes it.
struct AttributeReader {
    std::string_view name;
    Request (*read)(const NodeRef& node, const std::string& word);
};

template<typename Attribute>
Request length_attribute(const NodeRef& node, const std::string& word) {
    return Intent{Set{Attribute{node, length_of(word)}}};
}

template<typename Attribute>
Request paint_attribute(const NodeRef& node, const std::string& word) {
    return Intent{Set{Attribute{node, Paint{color_of(word)}}}};
}

template<typename Attribute>
Request text_attribute(const NodeRef& node, const std::string& word) {
    return Intent{Set{Attribute{node, word}}};
}

Request transform_attribute(const NodeRef& node, const std::string& word) {
    if (word == "none") return Intent{Set{SetTransform{node, Transform{}}}};

    const std::optional<Transform> value = SvgValue::parse_transform(word);
    if (!value) {
        throw ParseFailed{"cannot read '" + word +
                          "' as a transform list: translate(x [y]), scale(x [y]), rotate(deg [cx cy]), "
                          "skewX(deg), skewY(deg), matrix(a b c d e f), or none"};
    }
    return Intent{Set{SetTransform{node, *value}}};
}

Request view_box_attribute(const NodeRef& node, const std::string& word) {
    const std::optional<Container::ViewBox> value = SvgValue::parse_view_box(word);
    if (!value) {
        throw ParseFailed{"cannot read '" + word +
                          "' as a viewBox: four finite numbers, with a non-negative width and height"};
    }
    return Intent{Set{SetViewBox{node, *value}}};
}

Request text_anchor_attribute(const NodeRef& node, const std::string& word) {
    const std::optional<Text::Anchor> value = SvgValue::parse_text_anchor(word);
    if (!value) {
        throw ParseFailed{"cannot read '" + word + "' as a text-anchor: start, middle or end"};
    }
    return Intent{Set{SetTextAnchor{node, *value}}};
}

// Every attribute a node can carry, in the order the help groups them: geometry, paint and
// the transform any element has, the root's canvas, and the parts of a text. The names come
// from the attribute types, so the word this table looks up and the word the Editor refuses
// are one spelling.
constexpr std::array<AttributeReader, 30> attributes{{
    {SetX::name, length_attribute<SetX>},
    {SetY::name, length_attribute<SetY>},
    {SetW::name, length_attribute<SetW>},
    {SetH::name, length_attribute<SetH>},
    {SetCx::name, length_attribute<SetCx>},
    {SetCy::name, length_attribute<SetCy>},
    {SetR::name, length_attribute<SetR>},
    {SetRx::name, length_attribute<SetRx>},
    {SetRy::name, length_attribute<SetRy>},
    {SetX1::name, length_attribute<SetX1>},
    {SetY1::name, length_attribute<SetY1>},
    {SetX2::name, length_attribute<SetX2>},
    {SetY2::name, length_attribute<SetY2>},
    {SetDx::name, length_attribute<SetDx>},
    {SetDy::name, length_attribute<SetDy>},
    {SetStrokeWidth::name, length_attribute<SetStrokeWidth>},
    {SetWidth::name, length_attribute<SetWidth>},
    {SetHeight::name, length_attribute<SetHeight>},
    {SetStartOffset::name, length_attribute<SetStartOffset>},
    {SetXmlns::name, text_attribute<SetXmlns>},
    {SetViewBox::name, view_box_attribute},
    {SetTextAnchor::name, text_anchor_attribute},
    {SetFontSize::name, length_attribute<SetFontSize>},
    {SetClass::name, text_attribute<SetClass>},
    {SetFill::name, paint_attribute<SetFill>},
    {SetStroke::name, paint_attribute<SetStroke>},
    {SetTransform::name, transform_attribute},
    {SetHref::name, text_attribute<SetHref>},
    {SetText::name, text_attribute<SetText>},
    {SetContent::name, text_attribute<SetContent>},
}};

}  // namespace

Request parse_set(const Words& words) {
    expect(words.size(), 4, "set takes '<id> <attribute> <value>'");
    const NodeRef node = reference_of(words[1]);

    for (const AttributeReader& attribute : attributes) {
        if (attribute.name == words[2]) return attribute.read(node, words[3]);
    }
    throw ParseFailed{"error: unknown attribute: " + quote_if_needed(words[2])};
}
