#include "Geometry.hh"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace Geometry {

namespace {

// --- units ---

// The length units the model converts on its own, and only those. mm and cm are a
// power of ten apart, so that conversion is exact and needs nothing outside the
// document. Everything else stays apart: px, pt and in are pinned to each other by
// CSS but not by a power of ten, and a user unit, an em and an ex are defined by the
// viewport or by font metrics, so nothing here can say how big one is.
//
// A user unit is deliberately not a px. "1 user unit + 1 px = 2px" only holds in a
// viewport whose user coordinate system happens to be one pixel wide, which is a
// property of the viewport and not of the value.
std::optional<float> factor_between(Length::Unit from, Length::Unit to) {
    if (from == to) return 1.0F;
    if (from == Length::Unit::Cm && to == Length::Unit::Mm) return 10.0F;
    if (from == Length::Unit::Mm && to == Length::Unit::Cm) return 0.1F;
    return std::nullopt;
}

// One side of an addition: the same kind of value, in a unit the model can convert,
// or nothing. The sum is written in the unit the value already had, so an element
// keeps the units it was written with.
std::optional<LengthPercentage> sum(LengthPercentage left, LengthPercentage right) {
    if (std::holds_alternative<Percentage>(left)) {
        if (!std::holds_alternative<Percentage>(right)) return std::nullopt;
        return Percentage{std::get<Percentage>(left).value + std::get<Percentage>(right).value};
    }
    const Length& a = std::get<Length>(left);
    if (std::holds_alternative<Percentage>(right)) return std::nullopt;
    const Length& b = std::get<Length>(right);
    const std::optional<float> factor = factor_between(b.unit, a.unit);
    if (!factor) return std::nullopt;
    return Length{a.value + b.value * *factor, a.unit};
}

std::string no_sum(std::string_view offset, std::string_view value) {
    return "the offset " + std::string{offset} + " does not add up with " + std::string{value} +
           ": of the length units only mm and cm convert, and a percentage has no sum with a length";
}

// Adds the offset to both coordinates of one point, and leaves both of them alone
// when one of the two cannot take it.
bool move_point(LengthPercentage& x, LengthPercentage dx, LengthPercentage& y, LengthPercentage dy,
                std::string& problem) {
    const std::optional<LengthPercentage> moved_x = sum(x, dx);
    if (!moved_x) {
        problem = no_sum(to_text(dx), to_text(x));
        return false;
    }
    const std::optional<LengthPercentage> moved_y = sum(y, dy);
    if (!moved_y) {
        problem = no_sum(to_text(dy), to_text(y));
        return false;
    }
    x = *moved_x;
    y = *moved_y;
    return true;
}

// --- transforms ---

// A translation a matrix can hold: a matrix counts in user units, so an offset in any
// other unit, or a percentage, cannot be written into one.
std::optional<float> user_units_of(LengthPercentage value) {
    if (!std::holds_alternative<Length>(value)) return std::nullopt;
    const Length& length = std::get<Length>(value);
    if (length.unit != Length::Unit::UserUnit) return std::nullopt;
    return length.value;
}

// The matrix a transform stands for. Scale and matrix are exact; a rotation needs its
// cosine and sine, which is where a matrix stops being exact.
struct ToMatrix {
    Matrix operator()(const Translate&) const {
        // A translate stays a translate: move_transform extends it instead of asking.
        throw std::logic_error("Geometry: a translate is never promoted to a matrix");
    }
    Matrix operator()(const Scale& scale) const { return Matrix{scale.x, 0.0F, 0.0F, scale.y, 0.0F, 0.0F}; }
    Matrix operator()(const Rotate& rotate) const {
        constexpr float pi = 3.14159265358979323846F;
        const float radians = rotate.degrees * pi / 180.0F;
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        return Matrix{cosine, sine, -sine, cosine, 0.0F, 0.0F};
    }
    Matrix operator()(const Matrix& matrix) const { return matrix; }
};

// A container has no coordinates to change, so a translation becomes its transform.
// Nothing here knows how big a user unit is, so an offset that cannot be written into
// the form the node already carries is refused rather than approximated.
bool move_transform(SVGNode& element, LengthPercentage dx, LengthPercentage dy, std::string& problem) {
    std::optional<Transform>& transform = element.transform_attr.transform;

    if (!transform) {
        transform = Translate{dx, dy};  // one translate says exactly what happened
        return true;
    }
    if (std::holds_alternative<Translate>(*transform)) {
        Translate& translation = std::get<Translate>(*transform);
        const std::optional<LengthPercentage> x = sum(translation.x, dx);
        if (!x) {
            problem = no_sum(to_text(dx), to_text(translation.x));
            return false;
        }
        const std::optional<LengthPercentage> y = sum(translation.y, dy);
        if (!y) {
            problem = no_sum(to_text(dy), to_text(translation.y));
            return false;
        }
        translation.x = *x;
        translation.y = *y;
        return true;
    }

    // Anything else becomes a matrix, because no single simple form holds a
    // translation next to a scale or a rotation. The translation lands in e and f,
    // which puts it in front of the transform the node already carried: the node moves
    // in its parent's coordinate system, which is what moving it means.
    const std::optional<float> offset_x = user_units_of(dx);
    const std::optional<float> offset_y = user_units_of(dy);
    if (!offset_x || !offset_y) {
        problem = "this node carries a scale, a rotation or a matrix, which has to become a matrix to take a "
                  "translation, and a matrix counts in user units: write the offset as a plain number";
        return false;
    }
    const Matrix existing = std::visit(ToMatrix{}, *transform);
    transform =
        Matrix{existing.a, existing.b, existing.c, existing.d, existing.e + *offset_x, existing.f + *offset_y};
    return true;
}

}  // namespace

bool translate(Node& payload, LengthPercentage dx, LengthPercentage dy, std::string& problem) {
    if (!std::holds_alternative<SVGNode>(payload)) {
        problem = "a text node has no coordinates and no transform to move";
        return false;
    }

    SVGNode& element = std::get<SVGNode>(payload);
    if (!std::holds_alternative<Shape>(element.data)) return move_transform(element, dx, dy, problem);

    // Every shape moves its own coordinates and nothing else: a rect its corner, a
    // circle and an ellipse their centre, a line both of its ends. Radii and sides stay
    // where they are, because a translation is not a scale.
    Shape& shape = std::get<Shape>(element.data);
    if (std::holds_alternative<Shape::Rect>(shape.specific)) {
        Shape::Rect& rect = std::get<Shape::Rect>(shape.specific);
        return move_point(rect.x, dx, rect.y, dy, problem);
    }
    if (std::holds_alternative<Shape::Circle>(shape.specific)) {
        Shape::Circle& circle = std::get<Shape::Circle>(shape.specific);
        return move_point(circle.cx, dx, circle.cy, dy, problem);
    }
    if (std::holds_alternative<Shape::Ellipse>(shape.specific)) {
        Shape::Ellipse& ellipse = std::get<Shape::Ellipse>(shape.specific);
        return move_point(ellipse.cx, dx, ellipse.cy, dy, problem);
    }

    Shape::Line& line = std::get<Shape::Line>(shape.specific);
    if (!move_point(line.x1, dx, line.y1, dy, problem)) return false;
    return move_point(line.x2, dx, line.y2, dy, problem);
}

}  // namespace Geometry
