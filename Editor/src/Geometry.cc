// What a move and a scale do to one node. A node that owns the values they change has them
// changed where they stand; a node that does not takes them in its transform, which is the
// arithmetic in Transform.cc. What a length is worth is Units.cc's business, and this file
// only asks it.
#include "Geometry.hh"

#include <optional>
#include <string>
#include <variant>

#include "Transform.hh"
#include "Units.hh"

namespace Geometry {

namespace {

// Adds the offset to both coordinates of one point, and leaves both of them alone when one
// of the two cannot take it.
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

bool translate(Node& payload, LengthPercentage dx, LengthPercentage dy, std::string& problem) {
    if (!std::holds_alternative<SVGNode>(payload)) {
        problem = "a text node has no coordinates and no transform to move";
        return false;
    }

    SVGNode& element = std::get<SVGNode>(payload);
    if (!std::holds_alternative<Shape>(element.data)) return move_transform(element, dx, dy, problem);

    // Every shape moves its own coordinates and nothing else: a rect its corner, a circle
    // and an ellipse their centre, a line both of its ends. Radii and sides stay where they
    // are, because a translation is not a scale.
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
    // A factor of nothing collapses the node and a negative one mirrors it. Both are things
    // SVG can write, and neither is written here yet, so they are refused rather than
    // approximated - and a factor that is not a number at all lands here too, because "not
    // greater than zero" is what both cases are.
    if (!(factor > 0.0F)) {
        problem = "error: scale factor must be positive";
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
