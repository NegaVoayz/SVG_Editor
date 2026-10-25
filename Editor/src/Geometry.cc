// What a move and a scale do to one node's own params. A node that does not own the values
// takes them in its transform instead; that is a Model chain edit, not arithmetic here.
// What a length is worth is Units.cc's business, and this file only asks it.
#include "Geometry.hh"

#include <cmath>
#include <optional>
#include <variant>

#include "Exhaustive.hh"
#include "Units.hh"

namespace Geometry {

namespace {

// Adds the offset to both coordinates of one point, and leaves both of them alone when one
// of the two cannot take it.
std::optional<Refusal> move_point(LengthPercentage& x, LengthPercentage dx, LengthPercentage& y,
                                  LengthPercentage dy) {
    const std::optional<LengthPercentage> moved_x = sum(x, dx);
    if (!moved_x) return Refusal{fault::IncompatibleLengths{dx, x}};
    if (!is_finite(*moved_x)) return Refusal{fault::NonFiniteValue{"moved coordinate"}};
    const std::optional<LengthPercentage> moved_y = sum(y, dy);
    if (!moved_y) return Refusal{fault::IncompatibleLengths{dy, y}};
    if (!is_finite(*moved_y)) return Refusal{fault::NonFiniteValue{"moved coordinate"}};
    x = *moved_x;
    y = *moved_y;
    return std::nullopt;
}

std::optional<Refusal> scale_value(LengthPercentage& value, float factor) {
    const LengthPercentage result = scaled(value, factor);
    if (!is_finite(result)) return Refusal{fault::NonFiniteValue{"scaled value"}};
    value = result;
    return std::nullopt;
}

// Every value a shape owns: a rect's corner and sides, a circle's centre and radius, an
// ellipse's two radii, both ends of a line, and every point of a polygon. A shape's
// transform is left alone here for the same reason a translation does not touch one: the
// values are what the shape is made of, and a transform would be a second answer to the
// same question.
struct ScaleShape {
    float factor;

    std::optional<Refusal> operator()(Shape::Rect& rect) const {
        if (std::optional<Refusal> problem = scale_value(rect.x, factor)) return problem;
        if (std::optional<Refusal> problem = scale_value(rect.y, factor)) return problem;
        if (std::optional<Refusal> problem = scale_value(rect.w, factor)) return problem;
        return scale_value(rect.h, factor);
    }
    std::optional<Refusal> operator()(Shape::Circle& circle) const {
        if (std::optional<Refusal> problem = scale_value(circle.cx, factor)) return problem;
        if (std::optional<Refusal> problem = scale_value(circle.cy, factor)) return problem;
        return scale_value(circle.r, factor);
    }
    std::optional<Refusal> operator()(Shape::Ellipse& ellipse) const {
        if (std::optional<Refusal> problem = scale_value(ellipse.cx, factor)) return problem;
        if (std::optional<Refusal> problem = scale_value(ellipse.cy, factor)) return problem;
        if (std::optional<Refusal> problem = scale_value(ellipse.rx, factor)) return problem;
        return scale_value(ellipse.ry, factor);
    }
    std::optional<Refusal> operator()(Shape::Line& line) const {
        if (std::optional<Refusal> problem = scale_value(line.x1, factor)) return problem;
        if (std::optional<Refusal> problem = scale_value(line.y1, factor)) return problem;
        if (std::optional<Refusal> problem = scale_value(line.x2, factor)) return problem;
        return scale_value(line.y2, factor);
    }
    std::optional<Refusal> operator()(Shape::Polygon& polygon) const {
        for (Shape::Point& point : polygon.points) {
            if (std::optional<Refusal> problem = scale_value(point.x, factor)) return problem;
            if (std::optional<Refusal> problem = scale_value(point.y, factor)) return problem;
        }
        return std::nullopt;
    }
    template <class T>
    std::optional<Refusal> operator()(T&) const {
        static_assert(always_false<T>, "no scaling rule for this shape alternative");
        return std::nullopt;
    }
};

struct TranslateShape {
    LengthPercentage dx;
    LengthPercentage dy;

    std::optional<Refusal> operator()(Shape::Rect& rect) const {
        return move_point(rect.x, dx, rect.y, dy);
    }
    std::optional<Refusal> operator()(Shape::Circle& circle) const {
        return move_point(circle.cx, dx, circle.cy, dy);
    }
    std::optional<Refusal> operator()(Shape::Ellipse& ellipse) const {
        return move_point(ellipse.cx, dx, ellipse.cy, dy);
    }
    std::optional<Refusal> operator()(Shape::Line& line) const {
        if (std::optional<Refusal> problem = move_point(line.x1, dx, line.y1, dy)) return problem;
        return move_point(line.x2, dx, line.y2, dy);
    }
    std::optional<Refusal> operator()(Shape::Polygon& polygon) const {
        for (Shape::Point& point : polygon.points) {
            if (std::optional<Refusal> problem = move_point(point.x, dx, point.y, dy)) return problem;
        }
        return std::nullopt;
    }
    template <class T>
    std::optional<Refusal> operator()(T&) const {
        static_assert(always_false<T>, "no translation rule for this shape alternative");
        return std::nullopt;
    }
};

std::optional<Refusal> translate_shape(Shape& shape, LengthPercentage dx, LengthPercentage dy) {
    // Every shape moves its own coordinates and nothing else: a rect its corner, a circle
    // and an ellipse their centre, a line both of its ends, and a polygon every point.
    // Radii and sides stay where they are, because a translation is not a scale.
    return std::visit(TranslateShape{dx, dy}, shape.specific);
}

// A <text>'s x and y are its position; an absent coordinate is the SVG default of zero,
// so the first move gives it the offset itself. A <tspan> or <textPath> with an absent
// coordinate inherits the position before it, so a move changes only coordinates that
// are written; writing an inherited coordinate would break that chain. The dx and dy
// adjustments are relative offsets and are not rewritten by a move.
std::optional<Refusal> translate_text(Text& text, LengthPercentage dx, LengthPercentage dy) {
    const bool owns_origin = std::holds_alternative<Text::Wrapper>(text.specific);
    const std::optional<LengthPercentage> x = present_length_percentage(text.common.x);
    const std::optional<LengthPercentage> y = present_length_percentage(text.common.y);
    std::optional<LengthPercentage> moved_x;
    if (!x) {
        if (owns_origin) moved_x = dx;
    } else {
        moved_x = sum(*x, dx);
        if (!moved_x) return Refusal{fault::IncompatibleLengths{dx, *x}};
    }
    if (moved_x && !is_finite(*moved_x)) return Refusal{fault::NonFiniteValue{"moved coordinate"}};

    std::optional<LengthPercentage> moved_y;
    if (!y) {
        if (owns_origin) moved_y = dy;
    } else {
        moved_y = sum(*y, dy);
        if (!moved_y) return Refusal{fault::IncompatibleLengths{dy, *y}};
    }
    if (moved_y && !is_finite(*moved_y)) return Refusal{fault::NonFiniteValue{"moved coordinate"}};

    if (moved_x) text.common.x = maybe_length_percentage(*moved_x);
    if (moved_y) text.common.y = maybe_length_percentage(*moved_y);
    return std::nullopt;
}

std::optional<Refusal> scale_optional(MaybeLengthPercentage& value, float factor) {
    std::optional<LengthPercentage> present = present_length_percentage(value);
    if (!present) return std::nullopt;
    if (std::optional<Refusal> problem = scale_value(*present, factor)) return problem;
    value = maybe_length_percentage(*present);
    return std::nullopt;
}

// Position, relative offsets and font size are all lengths the text owns. Scaling them
// keeps the text at the same place in the parent's coordinate system (the origin stays
// zero) and grows the type with everything else.
std::optional<Refusal> scale_text(Text& text, float factor) {
    if (std::optional<Refusal> problem = scale_optional(text.common.x, factor)) return problem;
    if (std::optional<Refusal> problem = scale_optional(text.common.y, factor)) return problem;
    if (std::optional<Refusal> problem = scale_optional(text.common.dx, factor)) return problem;
    if (std::optional<Refusal> problem = scale_optional(text.common.dy, factor)) return problem;
    if (std::optional<Refusal> problem = scale_optional(text.common.font_size, factor)) return problem;
    if (std::holds_alternative<Text::Path>(text.specific)) {
        Text::Path& path = std::get<Text::Path>(text.specific);
        return scale_optional(path.startOffset, factor);
    }
    return std::nullopt;
}

}  // namespace

std::optional<Refusal> translate_params(Node& payload, LengthPercentage dx, LengthPercentage dy) {
    if (!is_finite(dx)) return Refusal{fault::NonFiniteValue{"offset x"}};
    if (!is_finite(dy)) return Refusal{fault::NonFiniteValue{"offset y"}};
    if (!std::holds_alternative<SVGNode>(payload)) return Refusal{fault::NoDirectParams{}};

    SVGNode& element = std::get<SVGNode>(payload);
    if (std::holds_alternative<Shape>(element.data)) {
        return translate_shape(std::get<Shape>(element.data), dx, dy);
    }
    if (std::holds_alternative<Text>(element.data)) {
        return translate_text(std::get<Text>(element.data), dx, dy);
    }
    return Refusal{fault::NoDirectParams{}};
}

std::optional<Refusal> scale_params(Node& payload, float factor) {
    // A factor of nothing collapses the node and a negative one mirrors it. Both are things
    // SVG can write, and neither is written here yet, so they are refused rather than
    // approximated - and a factor that is not a number at all lands here too, because "not
    // greater than zero" is what both cases are.
    if (!std::isfinite(factor) || !(factor > 0.0F)) {
        return Refusal{fault::ScaleFactorNotPositive{}};
    }
    if (!std::holds_alternative<SVGNode>(payload)) return Refusal{fault::NoDirectParams{}};

    SVGNode& element = std::get<SVGNode>(payload);
    if (std::holds_alternative<Shape>(element.data)) {
        if (std::optional<Refusal> problem = std::visit(ScaleShape{factor}, std::get<Shape>(element.data).specific)) {
            return problem;
        }
    } else if (std::holds_alternative<Text>(element.data)) {
        if (std::optional<Refusal> problem = scale_text(std::get<Text>(element.data), factor)) return problem;
    } else {
        return Refusal{fault::NoDirectParams{}};
    }

    // Stroke width is a drawn dimension like a radius or a side. A scale changes it with
    // the geometry; a transform-based scale already covers it through the transform.
    return scale_optional(element.paint_attr.stroke_width, factor);
}

}  // namespace Geometry
