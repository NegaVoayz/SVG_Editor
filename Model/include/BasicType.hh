#pragma once
#include <charconv>
#include <cstdint>
#include <string>
#include <optional>
#include <variant>

// The same width as an MVCCHive virtual id: a handle is never narrowed on the way
// into or out of the hive.
using Id = std::uint64_t;

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

// The text a unit is written with, and nothing at all for a user unit: SVG writes that
// one as the bare number. A value needs it, and so does a pair of values that share one
// unit - a canvas of 800x600 pixels is "(800x600)px" and not "800pxx600px".
inline std::string_view unit_text(Length::Unit unit) {
    switch (unit) {
        case Length::Unit::UserUnit: return "";
        case Length::Unit::Em: return "em";
        case Length::Unit::Ex: return "ex";
        case Length::Unit::Px: return "px";
        case Length::Unit::Pt: return "pt";
        case Length::Unit::Mm: return "mm";
        case Length::Unit::Cm: return "cm";
        case Length::Unit::In: return "in";
    }
    return "";
}

// The text form of a length, as SVG writes it: the unit spelled out, except for a user
// unit, which is the bare number. The view prints values with this and the serializer
// will write them with it, so it belongs next to the type.
inline std::string to_text(const LengthPercentage& value) {
    if (std::holds_alternative<Percentage>(value)) {
        return number_text(std::get<Percentage>(value).value) + "%";
    }
    const Length& length = std::get<Length>(value);
    return number_text(length.value) + std::string{unit_text(length.unit)};
}
