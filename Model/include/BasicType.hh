#pragma once
#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>
#include <optional>
#include <type_traits>
#include <variant>

#include "Exhaustive.hh"

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

// A length or percentage that may be absent. Absence is an alternative rather than an
// optional wrapper so the type takes the same 12 bytes as LengthPercentage while every
// state remains explicit and exhaustively dispatchable.
struct Unset {};
using MaybeLengthPercentage = std::variant<Unset, Length, Percentage>;

// The primary template is the single compile-time failure point for conversion in either
// direction. Specializations below enumerate every alternative that has a meaning at the
// destination; adding another alternative cannot silently pass through.
template<typename T>
std::optional<LengthPercentage> present_length_percentage(const T&) {
    static_assert(always_false<T>,
                  "a MaybeLengthPercentage alternative has no LengthPercentage form");
    return std::nullopt;
}

template<>
inline std::optional<LengthPercentage> present_length_percentage<Unset>(const Unset&) {
    return std::nullopt;
}

template<>
inline std::optional<LengthPercentage> present_length_percentage<Length>(const Length& value) {
    return LengthPercentage{value};
}

template<>
inline std::optional<LengthPercentage> present_length_percentage<Percentage>(const Percentage& value) {
    return LengthPercentage{value};
}

inline std::optional<LengthPercentage> present_length_percentage(const MaybeLengthPercentage& value) {
    return std::visit(
        [](const auto& alternative) -> std::optional<LengthPercentage> {
            using T = std::remove_cvref_t<decltype(alternative)>;
            return present_length_percentage<T>(alternative);
        },
        value);
}

template<typename T>
MaybeLengthPercentage maybe_length_percentage(const T&) {
    static_assert(always_false<T>, "a LengthPercentage alternative has no MaybeLengthPercentage form");
    return Unset{};
}

template<>
inline MaybeLengthPercentage maybe_length_percentage<Length>(const Length& value) {
    return value;
}

template<>
inline MaybeLengthPercentage maybe_length_percentage<Percentage>(const Percentage& value) {
    return value;
}

inline MaybeLengthPercentage maybe_length_percentage(const LengthPercentage& value) {
    return std::visit(
        [](const auto& alternative) -> MaybeLengthPercentage {
            using T = std::remove_cvref_t<decltype(alternative)>;
            return maybe_length_percentage<T>(alternative);
        },
        value);
}

// The number a length or a percentage carries, without its unit. Validation and
// sign checks need the magnitude; rendering still uses to_text() so the unit survives.
inline float numeric_value(const LengthPercentage& value) {
    return std::holds_alternative<Percentage>(value) ? std::get<Percentage>(value).value
                                                     : std::get<Length>(value).value;
}

// Nothing in the model can carry a NaN or an infinity: SVG geometry is finite by
// definition, and a value that overflows its float has no representation to write back.
inline bool is_finite(const LengthPercentage& value) { return std::isfinite(numeric_value(value)); }

// The shortest spelling that reads back as the same float. This is the serialization
// policy: unlike display formatting, it never rounds a non-zero geometry value to zero.
inline std::string exact_number_text(float value) {
    if (value == 0.0F) value = 0.0F;  // normalize negative zero
    char buffer[64];
    const std::to_chars_result written = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (written.ec != std::errc{}) return std::to_string(value);
    return std::string(buffer, written.ptr);
}

// Display formatting keeps ordinary notation and up to six decimal places. If that
// would turn a present value into "0", the exact spelling is used instead.
inline std::string number_text(float value) {
    if (value == 0.0F) value = 0.0F;  // normalize negative zero
    char buffer[64];
    const std::to_chars_result written =
        std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, 6);
    if (written.ec != std::errc{}) return exact_number_text(value);
    std::string text(buffer, written.ptr);
    const std::size_t decimal = text.find('.');
    if (decimal != std::string::npos) {
        while (text.back() == '0') text.pop_back();
        if (text.back() == '.') text.pop_back();
    }
    if (value != 0.0F && (text == "0" || text == "-0")) return exact_number_text(value);
    return text;
}

// The text a unit is written with, and nothing at all for a user unit: SVG writes that
// one as the bare number. A value needs it; a place that displays two values together may
// put one suffix inside that display, as the canvas does with "(600 x 480 px)".
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

// The display form of a length. Serialization uses svg_text() below so presentation
// rounding cannot change the document.
inline std::string to_text(const LengthPercentage& value) {
    if (std::holds_alternative<Percentage>(value)) {
        return number_text(std::get<Percentage>(value).value) + "%";
    }
    const Length& length = std::get<Length>(value);
    return number_text(length.value) + std::string{unit_text(length.unit)};
}

inline std::string svg_text(const LengthPercentage& value) {
    if (std::holds_alternative<Percentage>(value)) {
        return exact_number_text(std::get<Percentage>(value).value) + "%";
    }
    const Length& length = std::get<Length>(value);
    return exact_number_text(length.value) + std::string{unit_text(length.unit)};
}
