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

// Lengths come in two systems here, and nothing adds up across them.
//
// The metric one is mm and cm, a power of ten apart. The other is the one CSS defines
// against the inch - 1in, 72pt, 96px - which is exact within itself. An inch is also
// exactly 25.4mm by definition, and yet 1mm + 1in is refused: a document that allowed
// it would hold 26.4mm, and whoever reads that value next cannot tell it was written
// in two systems. The table stops at the edge of each system for that reason, not
// because the arithmetic is impossible.
//
// A user unit, an em and an ex are in neither system. A user unit's size is whatever
// the viewport's user coordinate system says it is, and an em or an ex is font
// metrics, so none of the three has a size this could know. A user unit is
// deliberately not a px either: "1 user unit + 1 px = 2px" only holds in a viewport
// whose user coordinate system happens to be one pixel wide.
enum class System { Metric, Imperial };

std::optional<System> system_of(Length::Unit unit) {
    switch (unit) {
        case Length::Unit::Mm:
        case Length::Unit::Cm: return System::Metric;
        case Length::Unit::In:
        case Length::Unit::Pt:
        case Length::Unit::Px: return System::Imperial;
        case Length::Unit::UserUnit:
        case Length::Unit::Em:
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
        case Length::Unit::UserUnit:
        case Length::Unit::Em:
        case Length::Unit::Ex: return std::nullopt;
    }
    return std::nullopt;
}

// The factor that turns a value in one unit into the same length in another. It is
// only ever asked about two units of one system, so both have an anchor.
std::optional<float> factor_between(Length::Unit from, Length::Unit to) {
    if (from == to) return 1.0F;
    const std::optional<System> from_system = system_of(from);
    const std::optional<System> to_system = system_of(to);
    if (!from_system || from_system != to_system) return std::nullopt;
    return *per_anchor(to) / *per_anchor(from);
}

// Zero is exactly zero in every unit, so it neither needs a conversion nor blocks one:
// a coordinate of 0 can take an offset in any unit, and a zero offset leaves any
// coordinate alone. Without this, "move r 5px 5px" on a rect whose x and y are zero
// user units would be refused, which would make the first move of a fresh element
// depend on how it was written.
bool is_zero(LengthPercentage value) {
    if (std::holds_alternative<Percentage>(value)) return std::get<Percentage>(value).value == 0.0F;
    return std::get<Length>(value).value == 0.0F;
}

// One side of an addition: the same kind of value, in a unit the model can convert,
// or nothing. The sum is written in the unit the value already had, so an element
// keeps the units it was written with - except where one side is zero and the other
// brings its own unit.
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

// What a length counts in, in words. An offset that does not add up is the one
// refusal a user is most likely to have written by mistake - "1cm + 1in" - and what
// went wrong there is not visible in the two numbers, so the sentence says which
// system each side is in rather than only that they differ.
std::string_view system_name(Length::Unit unit) {
    switch (unit) {
        case Length::Unit::Mm:
        case Length::Unit::Cm: return "metric";
        case Length::Unit::In:
        case Length::Unit::Pt:
        case Length::Unit::Px: return "imperial";
        case Length::Unit::UserUnit: return "a user unit, which is whatever the viewport says";
        case Length::Unit::Em: return "an em, which is a font metric";
        case Length::Unit::Ex: return "an ex, which is a font metric";
    }
    return "no system";
}

// The sentence for an addition that has no sum: which two things were added, and
// why they do not add up.
std::string no_sum(std::string_view offset, LengthPercentage offset_value, std::string_view value,
                   LengthPercentage value_value) {
    std::string message = "the offset " + std::string{offset} + " does not add up with " + std::string{value};
    if (std::holds_alternative<Percentage>(offset_value) || std::holds_alternative<Percentage>(value_value)) {
        message += ": a percentage has no sum with a length";
    } else {
        message += ": " + std::string{offset} + " is " + std::string{system_name(std::get<Length>(offset_value).unit)} +
                   " and " + std::string{value} + " is " + std::string{system_name(std::get<Length>(value_value).unit)};
    }
    return message +
           " - mm and cm are metric, in, pt and px are defined against the inch, and the two systems are not mixed";
}

// Adds the offset to both coordinates of one point, and leaves both of them alone
// when one of the two cannot take it.
bool move_point(LengthPercentage& x, LengthPercentage dx, LengthPercentage& y, LengthPercentage dy,
                std::string& problem) {
    const std::optional<LengthPercentage> moved_x = sum(x, dx);
    if (!moved_x) {
        problem = no_sum(to_text(dx), dx, to_text(x), x);
        return false;
    }
    const std::optional<LengthPercentage> moved_y = sum(y, dy);
    if (!moved_y) {
        problem = no_sum(to_text(dy), dy, to_text(y), y);
        return false;
    }
    x = *moved_x;
    y = *moved_y;
    return true;
}

// --- transforms ---

// The matrix a rotation stands for: its cosine and sine, which is where a matrix
// stops being exact.
Matrix rotation_matrix(float degrees) {
    constexpr float pi = 3.14159265358979323846F;
    const float radians = degrees * pi / 180.0F;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return Matrix{cosine, sine, -sine, cosine, 0.0F, 0.0F};
}

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
    Matrix operator()(const Rotate& rotate) const { return rotation_matrix(rotate.degrees); }
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
            problem = no_sum(to_text(dx), dx, to_text(translation.x), translation.x);
            return false;
        }
        const std::optional<LengthPercentage> y = sum(translation.y, dy);
        if (!y) {
            problem = no_sum(to_text(dy), dy, to_text(translation.y), translation.y);
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

// --- scale ---

// One value, times the factor. A percentage stays a percentage and a length stays in
// its own unit: a scale is a multiplication and no addition, so there is nothing here
// for the unit table to convert and nothing that could mix two systems.
LengthPercentage scaled(LengthPercentage value, float factor) {
    if (std::holds_alternative<Percentage>(value)) return Percentage{std::get<Percentage>(value).value * factor};
    const Length& length = std::get<Length>(value);
    return Length{length.value * factor, length.unit};
}

// Every value a shape owns: a rect's corner and sides, a circle's centre and radius,
// an ellipse's two radii, and both ends of a line. A shape has no transform of its
// own here for the same reason a translation does not touch one: the values are what
// it is made of.
struct ScaleShape {
    float factor;

    void operator()(Shape::Rect& rect) const {
        rect.x = scaled(rect.x, factor);
        rect.y = scaled(rect.y, factor);
        rect.w = scaled(rect.w, factor);
        rect.h = scaled(rect.h, factor);
    }
    void operator()(Shape::Circle& circle) const {
        circle.cx = scaled(circle.cx, factor);
        circle.cy = scaled(circle.cy, factor);
        circle.r = scaled(circle.r, factor);
    }
    void operator()(Shape::Ellipse& ellipse) const {
        ellipse.cx = scaled(ellipse.cx, factor);
        ellipse.cy = scaled(ellipse.cy, factor);
        ellipse.rx = scaled(ellipse.rx, factor);
        ellipse.ry = scaled(ellipse.ry, factor);
    }
    void operator()(Shape::Line& line) const {
        line.x1 = scaled(line.x1, factor);
        line.y1 = scaled(line.y1, factor);
        line.x2 = scaled(line.x2, factor);
        line.y2 = scaled(line.y2, factor);
    }
};

// A node with no values of its own takes the scale in its transform. The scale goes
// *behind* whatever the node already carries - transform="translate(...) scale(k)"
// - so the node keeps the place a move gave it and what is inside it grows from the
// node's own origin. That is the same place a scale of every coordinate under the
// node would leave the drawing in, in one payload instead of every one of them.
bool scale_transform(SVGNode& element, float factor, std::string& problem) {
    std::optional<Transform>& transform = element.transform_attr.transform;

    if (!transform) {
        transform = Scale{factor, factor};  // a scale stays a scale
        return true;
    }
    if (std::holds_alternative<Scale>(*transform)) {
        Scale& scale = std::get<Scale>(*transform);
        scale.x *= factor;
        scale.y *= factor;
        return true;
    }
    if (std::holds_alternative<Rotate>(*transform)) {
        const Matrix turned = rotation_matrix(std::get<Rotate>(*transform).degrees);
        transform = Matrix{turned.a * factor, turned.b * factor, turned.c * factor, turned.d * factor, 0.0F, 0.0F};
        return true;
    }
    if (std::holds_alternative<Matrix>(*transform)) {
        Matrix& matrix = std::get<Matrix>(*transform);
        // the two translation columns stay where they are: this is a scale about the
        // node's own origin, not about wherever the matrix moves it to
        matrix.a *= factor;
        matrix.b *= factor;
        matrix.c *= factor;
        matrix.d *= factor;
        return true;
    }

    // A translation has to become a matrix to take a scale in front of it, and a
    // matrix counts in user units, so a translation written in a unit cannot be part
    // of one.
    const Translate& translation = std::get<Translate>(*transform);
    const std::optional<float> x = user_units_of(translation.x);
    const std::optional<float> y = user_units_of(translation.y);
    if (!x || !y) {
        problem = "this node was moved by " + to_text(translation.x) + " " + to_text(translation.y) +
                  ", which has to become a matrix to take a scale behind it, and a matrix counts in user units: "
                  "write the offset as a plain number";
        return false;
    }
    transform = Matrix{factor, 0.0F, 0.0F, factor, *x, *y};
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

bool scale(Node& payload, float factor, std::string& problem) {
    // A factor of nothing collapses the node and a negative one mirrors it. Both are
    // things SVG can write, and neither is written here yet, so they are refused
    // rather than approximated - and a factor that is not a number at all lands here
    // too, because "not greater than zero" is what both cases are.
    if (!(factor > 0.0F)) {
        problem = "a scale of " + number_text(factor) + " is not written yet: a factor has to be greater than zero";
        return false;
    }
    if (!std::holds_alternative<SVGNode>(payload)) {
        problem = "a text node has no coordinates and no transform to scale";
        return false;
    }

    SVGNode& element = std::get<SVGNode>(payload);
    if (!std::holds_alternative<Shape>(element.data)) return scale_transform(element, factor, problem);

    std::visit(ScaleShape{factor}, std::get<Shape>(element.data).specific);
    return true;
}

}  // namespace Geometry
