// What a move and a scale do to one node. A node that owns the values they change has them
// changed where they stand; a node that does not takes them in its transform, which is the
// arithmetic in Transform.cc. What a length is worth is Units.cc's business, and this file
// only asks it.
#include "Geometry.hh"

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
    const std::optional<LengthPercentage> moved_y = sum(y, dy);
    if (!moved_y) return Refusal{fault::IncompatibleLengths{dy, y}};
    x = *moved_x;
    y = *moved_y;
    return std::nullopt;
}

// Every value a shape owns: a rect's corner and sides, a circle's centre and radius, an
// ellipse's two radii, and both ends of a line. A shape's transform is left alone here for
// the same reason a translation does not touch one: the values are what the shape is made
// of, and a transform would be a second answer to the same question.
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

}  // namespace

std::optional<Refusal> translate(Node& payload, LengthPercentage dx, LengthPercentage dy) {
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
    if (!(factor > 0.0F)) {
        return Refusal{fault::ScaleFactorNotPositive{}};
    }
    if (!std::holds_alternative<SVGNode>(payload)) {
        return Refusal{fault::TextCannotScale{}};
    }

    SVGNode& element = std::get<SVGNode>(payload);
    if (!std::holds_alternative<Shape>(element.data)) return scale_transform(element, factor);

    std::visit(ScaleShape{factor}, std::get<Shape>(element.data).specific);
    return std::nullopt;
}

}  // namespace Geometry
