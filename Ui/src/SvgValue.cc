#include "SvgValue.hh"

#include <charconv>
#include <cctype>
#include <cmath>
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

// A number the model can hold: finite as a double and still finite after narrowing to the
// float the model stores. "1e308" is finite as a double but overflows that float, so it
// cannot be written into a document and is refused here rather than becoming infinity.
std::optional<float> finite_float(double value) {
    if (!std::isfinite(value)) return std::nullopt;
    const float narrowed = static_cast<float>(value);
    if (!std::isfinite(narrowed)) return std::nullopt;
    return narrowed;
}

std::optional<float> number_of(std::string_view text) {
    if (text.empty()) return std::nullopt;
    double value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    return finite_float(value);
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

std::string ascii_lower(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char character : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
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
        if (!std::isfinite(channel_value) || channel_value < 0.0F || channel_value > 255.0F) {
            return std::nullopt;
        }
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
    const std::optional<float> narrowed = finite_float(value);
    if (!narrowed) return std::nullopt;
    if (unit == "%") return Percentage{*narrowed};

    const std::optional<Length::Unit> named = unit_of(unit);
    if (!named) return std::nullopt;
    return Length{*narrowed, *named};
}

std::optional<Color> parse_color(std::string_view text) {
    const std::string lowered = ascii_lower(text);
    if (lowered == "none") return Color{Color::None{}};
    if (lowered == "currentcolor") return Color{Color::Current{}};
    if (const std::optional<Color::NamedColorId> named = named_color_of(text)) {
        return Color{Color::Named{*named}};
    }
    if (const std::optional<Color::SystemColorId> system = system_color_of(text)) {
        return Color{Color::System{*system}};
    }
    if (text.starts_with("rgb(")) return parse_rgb_function(text);
    if (!text.starts_with("#")) return std::nullopt;

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

std::optional<Transform> parse_transform_step(std::string_view name, const std::vector<std::string_view>& words) {
    if (name == "translate" && (words.size() == 1 || words.size() == 2)) {
        const std::optional<LengthPercentage> x = parse_length(words[0]);
        if (!x) return std::nullopt;
        if (words.size() == 1) return translated(*x, Length{0.0F, Length::Unit::UserUnit});
        const std::optional<LengthPercentage> y = parse_length(words[1]);
        if (!y) return std::nullopt;
        return translated(*x, *y);
    }

    if (name == "scale" && (words.size() == 1 || words.size() == 2)) {
        const std::optional<float> x = number_of(words[0]);
        if (!x) return std::nullopt;
        const std::optional<float> y = words.size() == 1 ? x : number_of(words[1]);
        if (!y) return std::nullopt;
        return scaled(*x, *y);
    }

    if (name == "rotate" && (words.size() == 1 || words.size() == 3)) {
        const std::optional<float> degrees = number_of(words[0]);
        if (!degrees) return std::nullopt;
        if (words.size() == 1) return rotated(*degrees);

        const std::optional<LengthPercentage> x = parse_length(words[1]);
        const std::optional<LengthPercentage> y = parse_length(words[2]);
        if (!x || !y) return std::nullopt;
        return rotation_about(*degrees, *x, *y);
    }

    if (name == "skewX" && words.size() == 1) {
        const std::optional<float> degrees = number_of(words[0]);
        if (!degrees) return std::nullopt;
        return skewed_x(*degrees);
    }

    if (name == "skewY" && words.size() == 1) {
        const std::optional<float> degrees = number_of(words[0]);
        if (!degrees) return std::nullopt;
        return skewed_y(*degrees);
    }

    if (name == "matrix" && words.size() == 6) {
        float value[6] = {};
        for (std::size_t at = 0; at < 6; ++at) {
            const std::optional<float> number = number_of(words[at]);
            if (!number) return std::nullopt;
            value[at] = *number;
        }
        return matrix_transform(value[0], value[1], value[2], value[3], value[4], value[5]);
    }

    return std::nullopt;
}

std::optional<Transform> parse_transform(std::string_view text) {
    if (text.find_first_not_of(" \t") == std::string_view::npos) return Transform{};

    Transform result;
    std::size_t index = 0;
    bool any = false;
    while (index < text.size()) {
        while (index < text.size() && (text[index] == ' ' || text[index] == '\t' || text[index] == ',')) ++index;
        if (index == text.size()) break;

        std::size_t name_start = index;
        while (index < text.size() && text[index] != '(') ++index;
        if (index == text.size()) return std::nullopt;
        while (name_start < index && (text[name_start] == ' ' || text[name_start] == '\t')) ++name_start;
        std::size_t name_end = index;
        while (name_end > name_start && (text[name_end - 1] == ' ' || text[name_end - 1] == '\t')) --name_end;
        const std::string_view name = text.substr(name_start, name_end - name_start);

        const std::size_t body_start = ++index;
        while (index < text.size() && text[index] != ')') ++index;
        if (index == text.size()) return std::nullopt;
        const std::string_view body = text.substr(body_start, index - body_start);
        ++index;

        // One value per comma or run of spaces. An empty part is not a value, so the
        // trailing comma SVG tolerates is not one either.
        std::vector<std::string_view> words;
        std::size_t at = 0;
        while (at < body.size()) {
            while (at < body.size() && (body[at] == ' ' || body[at] == ',')) ++at;
            const std::size_t start = at;
            while (at < body.size() && body[at] != ' ' && body[at] != ',') ++at;
            if (start != at) words.push_back(body.substr(start, at - start));
        }

        std::optional<Transform> step = parse_transform_step(name, words);
        if (!step) return std::nullopt;
        const std::optional<Transform> composed = compose(result, *step);
        if (!composed) return std::nullopt;
        result = *composed;
        any = true;
    }
    if (!any) return std::nullopt;
    return result;
}

std::optional<Container::ViewBox> parse_view_box(std::string_view text) {
    float values[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    std::size_t index = 0;
    for (int part = 0; part < 4; ++part) {
        while (index < text.size() &&
               (text[index] == ' ' || text[index] == '\t' || text[index] == ',' || text[index] == '\r' ||
                text[index] == '\n')) {
            ++index;
        }
        if (index == text.size()) return std::nullopt;

        double value = 0;
        const char* begin = text.data() + index;
        const char* end = text.data() + text.size();
        const std::from_chars_result parsed = std::from_chars(begin, end, value);
        if (parsed.ec != std::errc{} || parsed.ptr == begin) return std::nullopt;
        const std::optional<float> narrowed = finite_float(value);
        if (!narrowed) return std::nullopt;
        values[part] = *narrowed;
        index = static_cast<std::size_t>(parsed.ptr - text.data());
    }

    while (index < text.size() &&
           (text[index] == ' ' || text[index] == '\t' || text[index] == ',' || text[index] == '\r' ||
            text[index] == '\n')) {
        ++index;
    }
    if (index != text.size() || values[2] < 0.0F || values[3] < 0.0F) return std::nullopt;
    return Container::ViewBox{values[0], values[1], values[2], values[3]};
}

std::optional<Text::Anchor> parse_text_anchor(std::string_view text) {
    const std::string lowered = ascii_lower(text);
    if (lowered == "start") return Text::Anchor::Start;
    if (lowered == "middle") return Text::Anchor::Middle;
    if (lowered == "end") return Text::Anchor::End;
    return std::nullopt;
}

}  // namespace SvgValue
