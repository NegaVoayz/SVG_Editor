#pragma once
#include <charconv>
#include <cstdint>
#include <string>
#include <variant>
#include <optional>

using Id = size_t;

struct Length {
    float value;
    enum class Unit {
        UserUnit, Em,
        Ex, Px, Pt, Mm,
        Cm, In
    } unit;
};

struct Percentage {
    float value;
};

using LengthPercentage = std::variant<Length,Percentage>;

// The shortest text a float has: "5" rather than "5.000000", which is what SVG
// source and a status line both want to read.
inline std::string number_text(float value) {
    char buffer[32];
    const std::to_chars_result written = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (written.ec != std::errc{}) return std::to_string(value);
    return std::string(buffer, written.ptr);
}

// The text form of a length, as SVG writes it: the unit spelled out, except for a
// user unit, which is the bare number. The view prints values with this and the
// serializer will write them with it, so it belongs next to the type.
inline std::string to_text(const LengthPercentage& value) {
    if (std::holds_alternative<Percentage>(value)) {
        return number_text(std::get<Percentage>(value).value) + "%";
    }
    const Length& length = std::get<Length>(value);
    switch (length.unit) {
        case Length::Unit::UserUnit: return number_text(length.value);
        case Length::Unit::Em: return number_text(length.value) + "em";
        case Length::Unit::Ex: return number_text(length.value) + "ex";
        case Length::Unit::Px: return number_text(length.value) + "px";
        case Length::Unit::Pt: return number_text(length.value) + "pt";
        case Length::Unit::Mm: return number_text(length.value) + "mm";
        case Length::Unit::Cm: return number_text(length.value) + "cm";
        case Length::Unit::In: return number_text(length.value) + "in";
    }
    return number_text(length.value);
}

struct Color {
    enum class SystemColorId : uint32_t {
        AccentColor, AccentColorText, ActiveText, ButtonBorder, 
        ButtonFace, ButtonText, Canvas, CanvasText, 
        Field, FieldText, GrayText, Highlight, 
        HighlightText, LinkText, Mark, MarkText, 
        SelectedItem, SelectedItemText, VisitedText
    };
    enum class NamedColorId : uint32_t {
        //todo: fill the enums
    };

    struct None {};
    struct Current {};
    struct System { SystemColorId id; };
    struct Named { NamedColorId id; };
    struct RGB { uint8_t r, g, b; };
    struct FuncRGB { uint8_t r, g, b; };

    std::variant<
    Color::None, Color::Current, Color::System,
    Color::Named, Color::RGB, Color::FuncRGB> specific;
};

struct PaintServer {
    std::string url;
    std::optional<Color> fallback;
};

using Paint = std::variant<
    Color,
    PaintServer         // url(#gradient) + fallback
>;
