// The unit table.
//
// Lengths come in two systems here, and nothing adds up across them. The metric one is mm
// and cm, a power of ten apart. The other is the one CSS defines against the inch - 1in,
// 72pt, 96px - which is exact within itself. An inch is also exactly 25.4mm by definition,
// and yet 1mm + 1in is refused: a document that allowed it would hold 26.4mm, and whoever
// reads that value next cannot tell it was written in two systems. The table stops at the
// edge of each system for that reason, not because the arithmetic is impossible.
//
// A user unit, an em and an ex are in neither system. A user unit's size is whatever the
// viewport's user coordinate system says it is, and an em or an ex is font metrics, so none
// of the three has a size this could know. A user unit is deliberately not a px either:
// "1 user unit + 1 px = 2px" only holds in a viewport whose user coordinate system happens
// to be one pixel wide.
#include "Units.hh"

#include <optional>
#include <variant>

namespace Geometry {

namespace {

enum class System { Metric, Imperial };

std::optional<System> system_of(Length::Unit unit) {
    switch (unit) {
        case Length::Unit::Mm: [[fallthrough]]
        case Length::Unit::Cm: return System::Metric;
        case Length::Unit::In: [[fallthrough]]
        case Length::Unit::Pt: [[fallthrough]]
        case Length::Unit::Px: return System::Imperial;
        case Length::Unit::UserUnit: [[fallthrough]]
        case Length::Unit::Em: [[fallthrough]]
        case Length::Unit::Ex: return std::nullopt;
    }
    return std::nullopt;
}

// How many of one unit there are in the unit its system is anchored at: a cm for the
// metric system, an inch for the other.
std::optional<float> per_anchor(Length::Unit unit) {
    switch (unit) {
        case Length::Unit::Mm: return 10.0F;
        case Length::Unit::Cm: return 1.0F;
        case Length::Unit::In: return 1.0F;
        case Length::Unit::Pt: return 72.0F;
        case Length::Unit::Px: return 96.0F;
        case Length::Unit::UserUnit: [[fallthrough]]
        case Length::Unit::Em: [[fallthrough]]
        case Length::Unit::Ex: return std::nullopt;
    }
    return std::nullopt;
}

// The factor that turns a value in one unit into the same length in another. It is only
// ever asked about two units of one system, so both have an anchor.
std::optional<float> factor_between(Length::Unit from, Length::Unit to) {
    if (from == to) return 1.0F;
    const std::optional<System> from_system = system_of(from);
    const std::optional<System> to_system = system_of(to);
    if (!from_system || from_system != to_system) return std::nullopt;
    return *per_anchor(to) / *per_anchor(from);
}

// Zero is exactly zero in every unit, so it neither needs a conversion nor blocks one: a
// coordinate of 0 can take an offset in any unit, and a zero offset leaves any coordinate
// alone. Without this, "move r 5px 5px" on a rect whose x and y are zero user units would
// be refused, which would make the first move of a fresh element depend on how it was
// written.
bool is_zero(LengthPercentage value) {
    if (std::holds_alternative<Percentage>(value)) return std::get<Percentage>(value).value == 0.0F;
    return std::get<Length>(value).value == 0.0F;
}

}  // namespace

std::optional<LengthPercentage> sum(LengthPercentage left, LengthPercentage right) {
    if (is_zero(right)) return left;
    if (is_zero(left)) return right;
    if (std::holds_alternative<Percentage>(left)) {
        if (!std::holds_alternative<Percentage>(right)) return std::nullopt;
        return Percentage{std::get<Percentage>(left).value + std::get<Percentage>(right).value};
    }
    const Length& a = std::get<Length>(left);
    if (std::holds_alternative<Percentage>(right)) return std::nullopt;
    const Length& b = std::get<Length>(right);
    if (a.unit == b.unit) return Length{a.value + b.value, a.unit};
    const std::optional<float> factor = factor_between(b.unit, a.unit);
    if (!factor) return std::nullopt;
    return Length{a.value + b.value * *factor, a.unit};
}

LengthPercentage scaled(LengthPercentage value, float factor) {
    if (std::holds_alternative<Percentage>(value)) return Percentage{std::get<Percentage>(value).value * factor};
    const Length& length = std::get<Length>(value);
    return Length{length.value * factor, length.unit};
}

}  // namespace Geometry
