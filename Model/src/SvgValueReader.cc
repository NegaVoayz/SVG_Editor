#include "SvgValueReader.hh"

#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Color.hh"

namespace SvgValueReader {

namespace {

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
                             text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

std::string ascii_lower(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char character : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
}

std::optional<float> number(std::string_view text) {
    text = trimmed(text);
    if (text.empty()) return std::nullopt;
    double value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    if (!std::isfinite(value)) return std::nullopt;
    const float narrowed = static_cast<float>(value);
    if (!std::isfinite(narrowed)) return std::nullopt;
    return narrowed;
}

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

std::optional<Color> rgb_function(std::string_view text) {
    if (!text.starts_with("rgb(") || !text.ends_with(")")) return std::nullopt;
    text.remove_prefix(4);
    text.remove_suffix(1);

    uint8_t channels[3] = {0, 0, 0};
    for (int index = 0; index < 3; ++index) {
        const std::size_t comma = text.find(',');
        const bool last = index == 2;
        if (last ? comma != std::string_view::npos : comma == std::string_view::npos) return std::nullopt;

        std::string_view part = text.substr(0, comma);
        if (!last) text.remove_prefix(comma + 1);
        part = trimmed(part);

        const bool percent = part.ends_with("%");
        if (percent) part.remove_suffix(1);
        const std::optional<float> value = number(part);
        if (!value) return std::nullopt;

        const float channel = percent ? *value * 255.0F / 100.0F : *value;
        if (!std::isfinite(channel) || channel < 0.0F || channel > 255.0F) return std::nullopt;
        channels[index] = static_cast<uint8_t>(channel + 0.5F);
    }
    return Color{Color::FuncRGB{channels[0], channels[1], channels[2]}};
}

std::vector<std::string_view> values_of(std::string_view text) {
    std::vector<std::string_view> values;
    std::size_t index = 0;
    while (index < text.size()) {
        while (index < text.size() &&
               (text[index] == ' ' || text[index] == '\t' || text[index] == '\r' || text[index] == '\n' ||
                text[index] == ',')) {
            ++index;
        }
        const std::size_t start = index;
        while (index < text.size() && text[index] != ' ' && text[index] != '\t' && text[index] != '\r' &&
               text[index] != '\n' && text[index] != ',') {
            ++index;
        }
        if (start != index) values.push_back(text.substr(start, index - start));
    }
    return values;
}

struct TransformFunction {
    std::string_view name;
    std::vector<std::string_view> values;
};

std::optional<std::vector<TransformFunction>> transform_functions(std::string_view text) {
    std::vector<TransformFunction> functions;
    std::size_t index = 0;
    while (index < text.size()) {
        while (index < text.size() &&
               (text[index] == ' ' || text[index] == '\t' || text[index] == '\r' || text[index] == '\n' ||
                text[index] == ',')) {
            ++index;
        }
        if (index == text.size()) break;

        const std::size_t name_start = index;
        while (index < text.size() && text[index] != '(') ++index;
        if (index == text.size()) return std::nullopt;
        const std::string_view name = trimmed(text.substr(name_start, index - name_start));
        if (name.empty()) return std::nullopt;

        const std::size_t body_start = ++index;
        while (index < text.size() && text[index] != ')') ++index;
        if (index == text.size()) return std::nullopt;
        functions.push_back(TransformFunction{name, values_of(text.substr(body_start, index - body_start))});
        ++index;
    }
    return functions.empty() ? std::nullopt : std::optional<std::vector<TransformFunction>>{std::move(functions)};
}

std::optional<Transform> one_transform(const TransformFunction& function) {
    const std::vector<std::string_view>& values = function.values;
    if (function.name == "translate" && (values.size() == 1 || values.size() == 2)) {
        const std::optional<LengthPercentage> x = SvgValueReader::length(values[0]);
        if (!x) return std::nullopt;
        if (values.size() == 1) return Transform{Translate{*x, Length{0.0F, Length::Unit::UserUnit}}};
        const std::optional<LengthPercentage> y = SvgValueReader::length(values[1]);
        if (!y) return std::nullopt;
        return Transform{Translate{*x, *y}};
    }

    if (function.name == "scale" && (values.size() == 1 || values.size() == 2)) {
        const std::optional<float> x = number(values[0]);
        if (!x) return std::nullopt;
        const std::optional<float> y = values.size() == 1 ? x : number(values[1]);
        if (!y) return std::nullopt;
        return Transform{Scale{*x, *y}};
    }

    if (function.name == "rotate" && (values.size() == 1 || values.size() == 3)) {
        const std::optional<float> degrees = number(values[0]);
        if (!degrees) return std::nullopt;
        if (values.size() == 1) return Transform{Rotate{*degrees}};

        const std::optional<LengthPercentage> x = SvgValueReader::length(values[1]);
        const std::optional<LengthPercentage> y = SvgValueReader::length(values[2]);
        if (!x || !y) return std::nullopt;
        return Transform{Rotate{*degrees, Rotate::Center{*x, *y}}};
    }

    if (function.name == "skewX" && values.size() == 1) {
        const std::optional<float> degrees = number(values[0]);
        if (!degrees) return std::nullopt;
        return Transform{SkewX{*degrees}};
    }

    if (function.name == "skewY" && values.size() == 1) {
        const std::optional<float> degrees = number(values[0]);
        if (!degrees) return std::nullopt;
        return Transform{SkewY{*degrees}};
    }

    if (function.name == "matrix" && values.size() == 6) {
        Matrix matrix{};
        float* members[] = {&matrix.a, &matrix.b, &matrix.c, &matrix.d, &matrix.e, &matrix.f};
        for (std::size_t index = 0; index < 6; ++index) {
            const std::optional<float> value = number(values[index]);
            if (!value) return std::nullopt;
            *members[index] = *value;
        }
        return Transform{matrix};
    }

    return std::nullopt;
}

}  // namespace

std::optional<LengthPercentage> length(std::string_view text) {
    text = trimmed(text);
    if (text.empty()) return std::nullopt;

    double value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr == begin) return std::nullopt;

    const std::string_view unit{parsed.ptr, static_cast<std::size_t>(end - parsed.ptr)};
    if (!std::isfinite(value)) return std::nullopt;
    const float narrowed = static_cast<float>(value);
    if (!std::isfinite(narrowed)) return std::nullopt;
    if (unit == "%") return Percentage{narrowed};

    const std::optional<Length::Unit> named = unit_of(unit);
    if (!named) return std::nullopt;
    return Length{narrowed, *named};
}

std::optional<Color> color(std::string_view text) {
    text = trimmed(text);
    const std::string lowered = ascii_lower(text);
    if (lowered == "none") return Color{Color::None{}};
    if (lowered == "currentcolor") return Color{Color::Current{}};
    if (const std::optional<Color::NamedColorId> named = named_color_of(text)) {
        return Color{Color::Named{*named}};
    }
    if (const std::optional<Color::SystemColorId> system = system_color_of(text)) {
        return Color{Color::System{*system}};
    }
    if (text.starts_with("rgb(")) return rgb_function(text);
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

std::optional<Paint> paint(std::string_view text) {
    text = trimmed(text);
    if (!text.starts_with("url(")) {
        const std::optional<Color> parsed = color(text);
        if (!parsed) return std::nullopt;
        return Paint{*parsed};
    }

    const std::size_t close = text.find(')');
    if (close == std::string_view::npos) return std::nullopt;
    const std::string_view target = trimmed(text.substr(4, close - 4));
    if (target.size() < 2 || target.front() != '#') return std::nullopt;

    PaintServer server;
    server.url = std::string{text.substr(0, close + 1)};
    const std::string_view fallback = trimmed(text.substr(close + 1));
    if (!fallback.empty()) {
        const std::optional<Color> parsed = color(fallback);
        if (!parsed) return std::nullopt;
        server.fallback = *parsed;
    }
    return Paint{std::move(server)};
}

std::optional<std::vector<Transform>> transform(std::string_view text) {
    text = trimmed(text);
    if (text.empty() || text == "none") return std::vector<Transform>{};

    const std::optional<std::vector<TransformFunction>> functions = transform_functions(text);
    if (!functions) return std::nullopt;

    std::vector<Transform> result;
    result.reserve(functions->size());
    for (const TransformFunction& function : *functions) {
        const std::optional<Transform> step = one_transform(function);
        if (!step) return std::nullopt;
        result.push_back(*step);
    }
    return result;
}

std::optional<Container::ViewBox> view_box(std::string_view text) {
    const std::vector<std::string_view> values = values_of(text);
    if (values.size() != 4) return std::nullopt;

    std::optional<float> numbers[4];
    for (std::size_t index = 0; index < 4; ++index) {
        numbers[index] = number(values[index]);
        if (!numbers[index]) return std::nullopt;
    }
    if (*numbers[2] < 0.0F || *numbers[3] < 0.0F) return std::nullopt;
    return Container::ViewBox{*numbers[0], *numbers[1], *numbers[2], *numbers[3]};
}

std::optional<Text::Anchor> text_anchor(std::string_view text) {
    const std::string lowered = ascii_lower(trimmed(text));
    if (lowered == "start") return Text::Anchor::Start;
    if (lowered == "middle") return Text::Anchor::Middle;
    if (lowered == "end") return Text::Anchor::End;
    return std::nullopt;
}

}  // namespace SvgValueReader
