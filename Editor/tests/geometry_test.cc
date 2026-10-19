// What moving a node's params does to it: a shape moves its coordinates, a text moves its
// position, and the transform arithmetic refuses what it cannot do exactly.
#include <cmath>
#include <cstdio>
#include <string>
#include <variant>

#include "Geometry.hh"
#include "Transform.hh"

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::printf("FAIL line %d: %s\n", line, expression);
    ++failures;
}

LengthPercentage mm(float value) { return Length{value, Length::Unit::Mm}; }
LengthPercentage cm(float value) { return Length{value, Length::Unit::Cm}; }
LengthPercentage user_units(float value) { return Length{value, Length::Unit::UserUnit}; }

SVGNode element_with(std::variant<Shape, Container, Text> data) {
    SVGNode element;
    element.identity_attr = IdentityAttr{"n", ""};
    element.data = std::move(data);
    return element;
}

Node rect(LengthPercentage x, LengthPercentage y) {
    return Node{element_with(Shape{Shape::Rect{x, y, mm(10), mm(10)}})};
}

Node circle() { return Node{element_with(Shape{Shape::Circle{user_units(0), user_units(0), user_units(5)}})}; }

Node line() {
    return Node{element_with(
        Shape{Shape::Line{user_units(0), user_units(0), user_units(5), user_units(5)}})};
}

Node group() { return Node{element_with(Container{ChildAttr{}, Container::Group{}})}; }

Node plain_text() { return Node{PlainText{"hello"}}; }

Node text_element() {
    Text text;
    text.common.x = user_units(1);
    text.common.y = user_units(2);
    text.common.font_size = user_units(10);
    text.child_attr = ChildAttr{};
    return Node{element_with(std::move(text))};
}

Node span_element() {
    Text text;
    text.specific = Text::Span{};
    text.child_attr = ChildAttr{};
    return Node{element_with(std::move(text))};
}

std::optional<Refusal> problem;

bool moved(Node& payload, LengthPercentage dx, LengthPercentage dy) {
    problem = Geometry::translate_params(payload, dx, dy);
    return !problem;
}

bool resized(Node& payload, float factor) {
    problem = Geometry::scale_params(payload, factor);
    return !problem;
}

bool moved_transform(Node& payload, LengthPercentage dx, LengthPercentage dy) {
    problem = Geometry::move_transform(std::get<SVGNode>(payload), dx, dy);
    return !problem;
}

bool resized_transform(Node& payload, float factor) {
    problem = Geometry::scale_transform(std::get<SVGNode>(payload), factor);
    return !problem;
}

const Shape& shape_of(const Node& payload) { return std::get<Shape>(std::get<SVGNode>(payload).data); }

const std::optional<Transform>& transform_of(const Node& payload) {
    return std::get<SVGNode>(payload).transform_attr.transform;
}

}  // namespace

#define CHECK(condition) check((condition), #condition, __LINE__)

int main() {
    // a shape moves its own coordinates, and only those
    Node box = rect(cm(1), cm(1));
    CHECK(moved(box, mm(5), mm(5)));
    CHECK(to_text(std::get<Shape::Rect>(shape_of(box).specific).x) == "1.5cm");
    CHECK(to_text(std::get<Shape::Rect>(shape_of(box).specific).w) == "10mm");

    Node dot = circle();
    CHECK(moved(dot, user_units(3), user_units(4)));
    CHECK(to_text(std::get<Shape::Circle>(shape_of(dot).specific).cx) == "3");
    CHECK(to_text(std::get<Shape::Circle>(shape_of(dot).specific).r) == "5");

    Node stroke = line();
    CHECK(moved(stroke, user_units(1), user_units(2)));
    CHECK(to_text(std::get<Shape::Line>(shape_of(stroke).specific).x2) == "6");
    CHECK(to_text(std::get<Shape::Line>(shape_of(stroke).specific).y2) == "7");

    // what the unit table does and does not convert: within a system, everything, and
    // across systems, nothing
    Node pixels = rect(Length{96.0F, Length::Unit::Px}, Length{0.0F, Length::Unit::Px});
    CHECK(moved(pixels, Length{1.0F, Length::Unit::In}, Length{0.0F, Length::Unit::Px}));
    CHECK(to_text(std::get<Shape::Rect>(shape_of(pixels).specific).x) == "192px");

    Node points = rect(Length{72.0F, Length::Unit::Pt}, Length{0.0F, Length::Unit::Pt});
    CHECK(moved(points, Length{1.0F, Length::Unit::In}, Length{0.0F, Length::Unit::Pt}));
    CHECK(to_text(std::get<Shape::Rect>(shape_of(points).specific).x) == "144pt");

    // an inch is exactly 25.4mm, and that is still not a sum: mixing the two systems
    // would leave the value in a system nobody chose
    Node metric = rect(mm(1), mm(1));
    CHECK(!moved(metric, Length{1.0F, Length::Unit::In}, mm(0)));
    CHECK(problem && std::holds_alternative<fault::IncompatibleLengths>(*problem));

    // and the boundary is the system, not the unit: a centimetre and an inch are no
    // more a sum than a millimetre and an inch are, even though 1in is exactly 2.54cm
    Node centimetres = rect(cm(1), cm(0));
    CHECK(!moved(centimetres, Length{1.0F, Length::Unit::In}, cm(0)));
    CHECK(problem && std::holds_alternative<fault::IncompatibleLengths>(*problem));
    if (problem && std::holds_alternative<fault::IncompatibleLengths>(*problem)) {
        const fault::IncompatibleLengths& lengths = std::get<fault::IncompatibleLengths>(*problem);
        CHECK(std::get<Length>(lengths.offset).unit == Length::Unit::In);
        CHECK(std::get<Length>(lengths.value).unit == Length::Unit::Cm);
    }

    Node relative = rect(Percentage{50.0F}, Percentage{0.0F});
    CHECK(!moved(relative, mm(10), mm(0)));
    CHECK(problem && std::holds_alternative<fault::IncompatibleLengths>(*problem));

    Node plain = rect(user_units(1), user_units(1));
    CHECK(!moved(plain, Length{5.0F, Length::Unit::Px}, user_units(0)));
    CHECK(problem && std::holds_alternative<fault::IncompatibleLengths>(*problem));
    if (problem && std::holds_alternative<fault::IncompatibleLengths>(*problem)) {
        const fault::IncompatibleLengths& lengths = std::get<fault::IncompatibleLengths>(*problem);
        CHECK(std::get<Length>(lengths.offset).unit == Length::Unit::Px);
        CHECK(std::get<Length>(lengths.value).unit == Length::Unit::UserUnit);
    }

    // zero is zero in every unit: a coordinate of 0 takes an offset in any unit, and a
    // zero offset leaves any coordinate alone
    Node fresh = rect(user_units(0), user_units(0));
    CHECK(moved(fresh, Length{5.0F, Length::Unit::Px}, Length{7.0F, Length::Unit::In}));
    CHECK(to_text(std::get<Shape::Rect>(shape_of(fresh).specific).x) == "5px");
    CHECK(to_text(std::get<Shape::Rect>(shape_of(fresh).specific).y) == "7in");

    Node kept = rect(Length{10.0F, Length::Unit::Mm}, Length{10.0F, Length::Unit::Mm});
    CHECK(moved(kept, user_units(0), Length{0.0F, Length::Unit::In}));
    CHECK(to_text(std::get<Shape::Rect>(shape_of(kept).specific).x) == "10mm");
    CHECK(to_text(std::get<Shape::Rect>(shape_of(kept).specific).y) == "10mm");

    // A container has no direct params, so the transform mode is what moves it.
    Node layer = group();
    CHECK(moved_transform(layer, user_units(2), user_units(3)));
    const std::optional<Transform>& first = std::get<SVGNode>(layer).transform_attr.transform;
    CHECK(first.has_value());
    CHECK(std::holds_alternative<Translate>(*first));
    CHECK(to_text(std::get<Translate>(*first).y) == "3");

    // two translates are one translate
    CHECK(moved_transform(layer, user_units(1), user_units(1)));
    CHECK(to_text(std::get<Translate>(*std::get<SVGNode>(layer).transform_attr.transform).x) == "3");

    // a scale has no simple form that holds a translation next to it, so it becomes a
    // matrix with the translation in e and f
    Node scaled = group();
    std::get<SVGNode>(scaled).transform_attr.transform = Scale{2.0F, 3.0F};
    CHECK(moved_transform(scaled, user_units(5), user_units(7)));
    const Matrix& matrix = std::get<Matrix>(*std::get<SVGNode>(scaled).transform_attr.transform);
    CHECK(matrix.a == 2.0F && matrix.d == 3.0F);
    CHECK(matrix.e == 5.0F && matrix.f == 7.0F);

    // a rotation becomes its cosine and sine, which is where a matrix stops being exact
    Node turned = group();
    std::get<SVGNode>(turned).transform_attr.transform = Rotate{90.0F};
    CHECK(moved_transform(turned, user_units(1), user_units(0)));
    const Matrix& rotated = std::get<Matrix>(*std::get<SVGNode>(turned).transform_attr.transform);
    CHECK(std::fabs(rotated.a) < 1e-6F);
    CHECK(std::fabs(rotated.b - 1.0F) < 1e-6F);
    CHECK(std::fabs(rotated.c + 1.0F) < 1e-6F);
    CHECK(rotated.e == 1.0F);

    // a matrix counts in user units, so an offset with a unit cannot go into one
    Node inches = group();
    std::get<SVGNode>(inches).transform_attr.transform = Scale{2.0F, 2.0F};
    CHECK(!moved_transform(inches, Length{1.0F, Length::Unit::In}, mm(0)));
    CHECK(problem && std::holds_alternative<fault::MatrixNeedsUserUnitsForMove>(*problem));

    // Character data has no position; a text element does, and its absent coordinates are
    // the SVG default of zero.
    Node words = plain_text();
    CHECK(!moved(words, user_units(1), user_units(1)));
    CHECK(problem && std::holds_alternative<fault::NoDirectParams>(*problem));

    Node label = text_element();
    CHECK(moved(label, user_units(2), user_units(3)));
    {
        const Text& moved_label = std::get<Text>(std::get<SVGNode>(label).data);
        CHECK(to_text(*moved_label.common.x) == "3");
        CHECK(to_text(*moved_label.common.y) == "5");
    }

    Node moved_label_transform = text_element();
    CHECK(moved_transform(moved_label_transform, user_units(2), user_units(3)));
    CHECK(transform_of(moved_label_transform).has_value());

    // A span with no x or y inherits where the text before it ended; a move must not
    // invent an origin for it.
    Node inherited_span = span_element();
    CHECK(moved(inherited_span, user_units(2), user_units(3)));
    {
        const Text& span = std::get<Text>(std::get<SVGNode>(inherited_span).data);
        CHECK(!span.common.x.has_value());
        CHECK(!span.common.y.has_value());
    }

    // --- scale ---

    // A shape's own values all grow: a rect's corner and its sides, and each one in
    // the unit it was written in, so a scale never has to convert anything
    Node grown_rect = rect(cm(1), cm(2));
    CHECK(resized(grown_rect, 2.0F));
    {
        const Shape::Rect& geometry = std::get<Shape::Rect>(shape_of(grown_rect).specific);
        CHECK(to_text(geometry.x) == "2cm");
        CHECK(to_text(geometry.y) == "4cm");
        CHECK(to_text(geometry.w) == "20mm");
        CHECK(to_text(geometry.h) == "20mm");
    }

    // a circle moves its centre and grows, a line grows at both ends
    Node grown_dot = circle();
    CHECK(resized(grown_dot, 3.0F));
    CHECK(to_text(std::get<Shape::Circle>(shape_of(grown_dot).specific).r) == "15");

    Node grown_line = line();
    CHECK(resized(grown_line, 2.0F));
    {
        const Shape::Line& ends = std::get<Shape::Line>(shape_of(grown_line).specific);
        CHECK(to_text(ends.x2) == "10");
        CHECK(to_text(ends.y2) == "10");
    }

    // a percentage is a percentage at any size
    Node grown_percent = rect(Percentage{50.0F}, Percentage{0.0F});
    CHECK(resized(grown_percent, 2.0F));
    CHECK(to_text(std::get<Shape::Rect>(shape_of(grown_percent).specific).x) == "100%");

    // A container's transform is scaled by the transform mode; a scale stays a scale.
    Node grown_group = group();
    CHECK(resized_transform(grown_group, 2.0F));
    CHECK(transform_of(grown_group).has_value());
    CHECK(std::holds_alternative<Scale>(*transform_of(grown_group)));
    CHECK(std::get<Scale>(*transform_of(grown_group)).x == 2.0F);
    CHECK(std::get<Scale>(*transform_of(grown_group)).y == 2.0F);
    CHECK(resized_transform(grown_group, 3.0F));
    CHECK(std::get<Scale>(*transform_of(grown_group)).y == 6.0F);

    // A matrix takes the scale in its four linear entries and keeps its translation:
    // this is a scale about the node's own origin, so the node stays where it was
    // put.
    Node stretched = group();
    std::get<SVGNode>(stretched).transform_attr.transform = Matrix{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
    CHECK(resized_transform(stretched, 2.0F));
    {
        const Matrix& matrix = std::get<Matrix>(*transform_of(stretched));
        CHECK(matrix.a == 2.0F && matrix.b == 4.0F && matrix.c == 6.0F && matrix.d == 8.0F);
        CHECK(matrix.e == 5.0F && matrix.f == 6.0F);
    }

    // A translation goes in front of the scale, which is the matrix form of
    // translate(...) scale(...): the node keeps the place a move gave it and what is
    // drawn under it grows from the node's own origin.
    Node shifted = group();
    std::get<SVGNode>(shifted).transform_attr.transform = Translate{user_units(5), user_units(7)};
    CHECK(resized_transform(shifted, 2.0F));
    {
        const Matrix& matrix = std::get<Matrix>(*transform_of(shifted));
        CHECK(matrix.a == 2.0F && matrix.d == 2.0F);
        CHECK(matrix.e == 5.0F && matrix.f == 7.0F);
        CHECK(matrix.b == 0.0F && matrix.c == 0.0F);
    }

    // a rotation needs its cosine and sine to take a scale, which is where a matrix
    // stops being exact
    Node turned_scaled = group();
    std::get<SVGNode>(turned_scaled).transform_attr.transform = Rotate{90.0F};
    CHECK(resized_transform(turned_scaled, 2.0F));
    {
        const Matrix& matrix = std::get<Matrix>(*transform_of(turned_scaled));
        CHECK(std::fabs(matrix.a) < 1e-6F);
        CHECK(std::fabs(matrix.b - 2.0F) < 1e-6F);
        CHECK(std::fabs(matrix.c + 2.0F) < 1e-6F);
        CHECK(std::fabs(matrix.d) < 1e-6F);
    }

    // and a translation in a unit cannot go into a matrix at all, so it is refused
    // rather than approximated
    Node metric_shift = group();
    std::get<SVGNode>(metric_shift).transform_attr.transform = Translate{mm(5), mm(7)};
    CHECK(!resized_transform(metric_shift, 2.0F));
    CHECK(problem && std::holds_alternative<fault::MatrixNeedsUserUnitsForScale>(*problem));

    // A text element's position and font size are values to multiply; character data has
    // no values of its own.
    Node characters = plain_text();
    CHECK(!resized(characters, 2.0F));
    CHECK(problem && std::holds_alternative<fault::NoDirectParams>(*problem));

    Node grown_text = text_element();
    CHECK(resized(grown_text, 2.0F));
    {
        const Text& text = std::get<Text>(std::get<SVGNode>(grown_text).data);
        CHECK(to_text(*text.common.x) == "2");
        CHECK(to_text(*text.common.y) == "4");
        CHECK(to_text(*text.common.font_size) == "20");
    }

    Node grown_text_transform = text_element();
    CHECK(resized_transform(grown_text_transform, 2.0F));
    CHECK(transform_of(grown_text_transform).has_value());

    // a factor of nothing, or a negative one, is not written yet: SVG can mirror and
    // collapse, and this cannot
    Node kept_factor = rect(user_units(1), user_units(1));
    CHECK(!resized(kept_factor, 0.0F));
    CHECK(problem && std::holds_alternative<fault::ScaleFactorNotPositive>(*problem));
    CHECK(!resized(kept_factor, -1.0F));
    CHECK(problem && std::holds_alternative<fault::ScaleFactorNotPositive>(*problem));
    CHECK(to_text(std::get<Shape::Rect>(shape_of(kept_factor).specific).x) == "1");

    if (failures != 0) {
        std::printf("geometry: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("geometry: all checks passed\n");
    return 0;
}
