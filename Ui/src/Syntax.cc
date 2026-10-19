// Everything a reader says when the words are not what the instruction asked for: the
// sentence for a line with the wrong number of words, and one word read as the value it
// spells - or the refusal when it spells nothing.
#include "Syntax.hh"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "SvgValue.hh"

namespace {

constexpr std::string_view length_syntax =
    "a length is a finite number, optionally with px, pt, cm, mm, in, em, ex, or a percentage";
constexpr std::string_view color_syntax =
    "a fill is #rgb, #rrggbb, rgb(r,g,b), a named or system colour, none or currentColor";
constexpr std::string_view factor_syntax = "a scale is a finite plain number greater than zero";

}  // namespace

void expect(std::size_t count, std::size_t wanted, std::string_view sentence) {
    if (count != wanted) throw ParseFailed{std::string{sentence}};
}

void expect_between(std::size_t count, std::size_t least, std::size_t most, std::string_view sentence) {
    if (count < least || count > most) throw ParseFailed{std::string{sentence}};
}

void expect_least(std::size_t count, std::size_t least, std::string_view sentence) {
    if (count < least) throw ParseFailed{std::string{sentence}};
}

LengthPercentage length_of(const std::string& word) {
    const std::optional<LengthPercentage> value = SvgValue::parse_length(word);
    if (!value) throw ParseFailed{"cannot read '" + word + "' as a length: " + std::string{length_syntax}};
    return *value;
}

Color color_of(const std::string& word) {
    const std::optional<Color> value = SvgValue::parse_color(word);
    if (!value) throw ParseFailed{"cannot read '" + word + "' as a fill: " + std::string{color_syntax}};
    return *value;
}

float factor_of(const std::string& word) {
    const std::optional<float> value = SvgValue::parse_number(word);
    if (!value) throw ParseFailed{"cannot read '" + word + "' as a scale: " + std::string{factor_syntax}};
    return *value;
}

// "/" is the root and anything else is an SVG id. A handle is the hive's internal id,
// not a name the user can see or type, so a word starting with '#' is just an id here.
NodeRef reference_of(const std::string& word) {
    if (word == "/") return RootRef{};
    return SvgIdRef{word};
}
