// What moving a node's params does to it: a shape moves its coordinates, a text moves its
// position, and the transform arithmetic refuses what it cannot do exactly.
#include <cmath>
#include <cstdio>
#include <string>
#include <variant>

#include "Geometry.hh"

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

Node plain_text() { return Node{PlainText{"hello"}}; }

Node text_element() {
    Text text;
    text.common.x = maybe_length_percentage(user_units(1));
    text.common.y = maybe_length_percentage(user_units(2));
    text.common.font_size = maybe_length_percentage(user_units(10));
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

const Shape& shape_of(const Node& payload) { return std::get<Shape>(std::get<SVGNode>(payload).data); }

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

    // Character data has no position; a text element does, and its absent coordinates are
    // the SVG default of zero.
    Node words = plain_text();
    CHECK(!moved(words, user_units(1), user_units(1)));
    CHECK(problem && std::holds_alternative<fault::NoDirectParams>(*problem));

    Node label = text_element();
    CHECK(moved(label, user_units(2), user_units(3)));
    {
        const Text& moved_label = std::get<Text>(std::get<SVGNode>(label).data);
        const std::optional<LengthPercentage> x = present_length_percentage(moved_label.common.x);
        const std::optional<LengthPercentage> y = present_length_percentage(moved_label.common.y);
        CHECK(x && to_text(*x) == "3");
        CHECK(y && to_text(*y) == "5");
    }

    // A span with no x or y inherits where the text before it ended; a move must not
    // invent an origin for it.
    Node inherited_span = span_element();
    CHECK(moved(inherited_span, user_units(2), user_units(3)));
    {
        const Text& span = std::get<Text>(std::get<SVGNode>(inherited_span).data);
        CHECK(!present_length_percentage(span.common.x).has_value());
        CHECK(!present_length_percentage(span.common.y).has_value());
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

    // Stroke width is a drawn dimension, so it grows with the geometry.
    Node stroked = rect(user_units(0), user_units(0));
    std::get<SVGNode>(stroked).paint_attr.stroke_width =
        maybe_length_percentage(user_units(2));
    CHECK(resized(stroked, 3.0F));
    const std::optional<LengthPercentage> stroke_width =
        present_length_percentage(std::get<SVGNode>(stroked).paint_attr.stroke_width);
    CHECK(stroke_width.has_value());
    if (stroke_width) {
        CHECK(to_text(*stroke_width) == "6");
    }

    // a percentage is a percentage at any size
    Node grown_percent = rect(Percentage{50.0F}, Percentage{0.0F});
    CHECK(resized(grown_percent, 2.0F));
    CHECK(to_text(std::get<Shape::Rect>(shape_of(grown_percent).specific).x) == "100%");

    // A text element's position and font size are values to multiply; character data has
    // no values of its own.
    Node characters = plain_text();
    CHECK(!resized(characters, 2.0F));
    CHECK(problem && std::holds_alternative<fault::NoDirectParams>(*problem));

    Node grown_text = text_element();
    CHECK(resized(grown_text, 2.0F));
    {
        const Text& text = std::get<Text>(std::get<SVGNode>(grown_text).data);
        const std::optional<LengthPercentage> x = present_length_percentage(text.common.x);
        const std::optional<LengthPercentage> y = present_length_percentage(text.common.y);
        const std::optional<LengthPercentage> font_size =
            present_length_percentage(text.common.font_size);
        CHECK(x && to_text(*x) == "2");
        CHECK(y && to_text(*y) == "4");
        CHECK(font_size && to_text(*font_size) == "20");
    }

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
