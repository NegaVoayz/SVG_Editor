// What a move and a scale do to one node. A node that owns the values they change has them
// changed where they stand; a node that does not takes them in its transform, which is the
// arithmetic in Transform.cc. What a length is worth is Units.cc's business, and this file
// only asks it.
#include "Geometry.hh"

#include <cmath>
#include <optional>
#include <variant>

#include "Transform.hh"
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
// ellipse's two radii, and both ends of a line. A shape's transform is left alone here for
// the same reason a translation does not touch one: the values are what the shape is made
// of, and a transform would be a second answer to the same question.
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
};

}  // namespace

std::optional<Refusal> translate(Node& payload, LengthPercentage dx, LengthPercentage dy) {
    if (!is_finite(dx)) return Refusal{fault::NonFiniteValue{"offset x"}};
    if (!is_finite(dy)) return Refusal{fault::NonFiniteValue{"offset y"}};
    if (!std::holds_alternative<SVGNode>(payload)) {
        return Refusal{fault::TextCannotMove{}};
    }

    SVGNode& element = std::get<SVGNode>(payload);
    if (!std::holds_alternative<Shape>(element.data)) return move_transform(element, dx, dy);

    // Every shape moves its own coordinates and nothing else: a rect its corner, a circle
    // and an ellipse their centre, a line both of its ends. Radii and sides stay where they
    // are, because a translation is not a scale.
    Shape& shape = std::get<Shape>(element.data);
    if (std::holds_alternative<Shape::Rect>(shape.specific)) {
        Shape::Rect& rect = std::get<Shape::Rect>(shape.specific);
        return move_point(rect.x, dx, rect.y, dy);
    }
    if (std::holds_alternative<Shape::Circle>(shape.specific)) {
        Shape::Circle& circle = std::get<Shape::Circle>(shape.specific);
        return move_point(circle.cx, dx, circle.cy, dy);
    }
    if (std::holds_alternative<Shape::Ellipse>(shape.specific)) {
        Shape::Ellipse& ellipse = std::get<Shape::Ellipse>(shape.specific);
        return move_point(ellipse.cx, dx, ellipse.cy, dy);
    }

    Shape::Line& line = std::get<Shape::Line>(shape.specific);
    if (std::optional<Refusal> problem = move_point(line.x1, dx, line.y1, dy)) return problem;
    return move_point(line.x2, dx, line.y2, dy);
}

std::optional<Refusal> scale(Node& payload, float factor) {
    // A factor of nothing collapses the node and a negative one mirrors it. Both are things
    // SVG can write, and neither is written here yet, so they are refused rather than
    // approximated - and a factor that is not a number at all lands here too, because "not
    // greater than zero" is what both cases are.
    if (!std::isfinite(factor) || !(factor > 0.0F)) {
        return Refusal{fault::ScaleFactorNotPositive{}};
    }
    if (!std::holds_alternative<SVGNode>(payload)) {
        return Refusal{fault::TextCannotScale{}};
    }

    SVGNode& element = std::get<SVGNode>(payload);
    if (!std::holds_alternative<Shape>(element.data)) return scale_transform(element, factor);

    return std::visit(ScaleShape{factor}, std::get<Shape>(element.data).specific);
}

}  // namespace Geometry
