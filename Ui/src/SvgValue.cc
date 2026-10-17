#include "SvgValue.hh"

#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace SvgValue {

namespace {

std::optional<Length::Unit> unit_of(std::string_view text) {
    if (text.empty()) return Length::Unit::UserUnit;
    if (text == "em") return Length::Unit::Em;
    if (text == "ex") return Length::Unit::Ex;
    if (text == "px") return Length::Unit::Px;
    if (text == "pt") return Length::Unit::Pt;
    if (text == "mm") return Length::Unit::Mm;
    if (text == "cm") return Length::Unit::Cm;
    if (text == "in") return Length::Unit::In;
    return std::nullopt;
}

std::optional<float> number_of(std::string_view text) {
    if (text.empty()) return std::nullopt;
    double value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    return static_cast<float>(value);
}

int hex_digit(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return -1;
}

std::optional<uint8_t> hex_byte(char high, char low) {
    const int top = hex_digit(high);
    const int bottom = hex_digit(low);
    if (top < 0 || bottom < 0) return std::nullopt;
    return static_cast<uint8_t>(top * 16 + bottom);
}

// "rgb(255, 0, 0)" and "rgb(100%, 0%, 0%)": the percentages are read as the 0..255
// the model stores, because the model has no room for a relative colour.
std::optional<Color> parse_rgb_function(std::string_view text) {
    if (!text.starts_with("rgb(") || !text.ends_with(")")) return std::nullopt;
    text.remove_prefix(4);
    text.remove_suffix(1);

    uint8_t channel[3] = {0, 0, 0};
    for (int index = 0; index < 3; ++index) {
        const std::size_t comma = text.find(',');
        const bool last = index == 2;
        if (last ? comma != std::string_view::npos : comma == std::string_view::npos) return std::nullopt;

        std::string_view part = text.substr(0, comma);
        if (!last) text.remove_prefix(comma + 1);
        while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
        while (!part.empty() && part.back() == ' ') part.remove_suffix(1);

        const bool percent = part.ends_with("%");
        if (percent) part.remove_suffix(1);
        const std::optional<float> value = number_of(part);
        if (!value) return std::nullopt;

        const float channel_value = percent ? *value * 255.0F / 100.0F : *value;
        if (channel_value < 0.0F || channel_value > 255.0F) return std::nullopt;
        channel[index] = static_cast<uint8_t>(channel_value + 0.5F);
    }
    return Color{Color::FuncRGB{channel[0], channel[1], channel[2]}};
}

}  // namespace

std::optional<LengthPercentage> parse_length(std::string_view text) {
    if (text.empty()) return std::nullopt;

    double value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr == begin) return std::nullopt;

    const std::string_view unit{parsed.ptr, static_cast<std::size_t>(end - parsed.ptr)};
    if (unit == "%") return Percentage{static_cast<float>(value)};

    const std::optional<Length::Unit> named = unit_of(unit);
    if (!named) return std::nullopt;
    return Length{static_cast<float>(value), *named};
}

std::optional<Color> parse_color(std::string_view text) {
    if (text == "none") return Color{Color::None{}};
    if (text == "currentColor") return Color{Color::Current{}};
    if (text.starts_with("rgb(")) return parse_rgb_function(text);
    if (!text.starts_with("#")) return std::nullopt;  // named colours have no ids yet

    const std::string_view digits = text.substr(1);
    if (digits.size() == 3) {
        const std::optional<uint8_t> r = hex_byte(digits[0], digits[0]);
        const std::optional<uint8_t> g = hex_byte(digits[1], digits[1]);
        const std::optional<uint8_t> b = hex_byte(digits[2], digits[2]);
        if (!r || !g || !b) return std::nullopt;
        return Color{Color::RGB{*r, *g, *b}};
    }
    if (digits.size() == 6) {
        const std::optional<uint8_t> r = hex_byte(digits[0], digits[1]);
        const std::optional<uint8_t> g = hex_byte(digits[2], digits[3]);
        const std::optional<uint8_t> b = hex_byte(digits[4], digits[5]);
        if (!r || !g || !b) return std::nullopt;
        return Color{Color::RGB{*r, *g, *b}};
    }
    return std::nullopt;
}

std::optional<float> parse_number(std::string_view text) { return number_of(text); }

std::optional<Transform> parse_transform(std::string_view text) {
    const std::size_t open = text.find('(');
    if (open == std::string_view::npos || !text.ends_with(')')) return std::nullopt;

    const std::string_view name = text.substr(0, open);
    std::string_view body = text.substr(open + 1, text.size() - open - 2);

    // One value per comma or run of spaces. An empty part is not a value, so the trailing
    // comma SVG tolerates is not one either.
    std::vector<std::string_view> words;
    std::size_t index = 0;
    while (index < body.size()) {
        while (index < body.size() && (body[index] == ' ' || body[index] == ',')) ++index;
        const std::size_t start = index;
        while (index < body.size() && body[index] != ' ' && body[index] != ',') ++index;
        if (start != index) words.push_back(body.substr(start, index - start));
    }

    if (name == "translate" && (words.size() == 1 || words.size() == 2)) {
        const std::optional<LengthPercentage> x = parse_length(words[0]);
        if (!x) return std::nullopt;
        if (words.size() == 1) return Transform{Translate{*x, Length{0.0F, Length::Unit::UserUnit}}};
        const std::optional<LengthPercentage> y = parse_length(words[1]);
        if (!y) return std::nullopt;
        return Transform{Translate{*x, *y}};
    }

    if (name == "scale" && (words.size() == 1 || words.size() == 2)) {
        const std::optional<float> x = number_of(words[0]);
        if (!x) return std::nullopt;
        const std::optional<float> y = words.size() == 1 ? x : number_of(words[1]);
        if (!y) return std::nullopt;
        return Transform{Scale{*x, *y}};
    }

    if (name == "rotate" && words.size() == 1) {
        const std::optional<float> degrees = number_of(words[0]);
        if (!degrees) return std::nullopt;
        return Transform{Rotate{*degrees}};
    }

    if (name == "matrix" && words.size() == 6) {
        float value[6] = {};
        for (std::size_t at = 0; at < 6; ++at) {
            const std::optional<float> number = number_of(words[at]);
            if (!number) return std::nullopt;
            value[at] = *number;
        }
        return Transform{Matrix{value[0], value[1], value[2], value[3], value[4], value[5]}};
    }

    return std::nullopt;
}

}  // namespace SvgValue
