// What moving a node does to it: a shape moves its coordinates, a container gets a
// transform, and the arithmetic refuses what it cannot do exactly.
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
    element.identity_attr = IdentityAttr{"n", "", no_id};
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

Node group() { return Node{element_with(Container{ChildAttr{no_id}, Container::Group{}})}; }

Node text_node() { return Node{PlainText{"hello"}}; }

bool moved(Node& payload, LengthPercentage dx, LengthPercentage dy, std::string& problem) {
    return Geometry::translate(payload, dx, dy, problem);
}

const Shape& shape_of(const Node& payload) { return std::get<Shape>(std::get<SVGNode>(payload).data); }

}  // namespace

#define CHECK(condition) check((condition), #condition, __LINE__)

int main() {
    std::string problem;

    // a shape moves its own coordinates, and only those
    Node box = rect(cm(1), cm(1));
    CHECK(moved(box, mm(5), mm(5), problem));
    CHECK(to_text(std::get<Shape::Rect>(shape_of(box).specific).x) == "1.5cm");
    CHECK(to_text(std::get<Shape::Rect>(shape_of(box).specific).w) == "10mm");

    Node dot = circle();
    CHECK(moved(dot, user_units(3), user_units(4), problem));
    CHECK(to_text(std::get<Shape::Circle>(shape_of(dot).specific).cx) == "3");
    CHECK(to_text(std::get<Shape::Circle>(shape_of(dot).specific).r) == "5");

    Node stroke = line();
    CHECK(moved(stroke, user_units(1), user_units(2), problem));
    CHECK(to_text(std::get<Shape::Line>(shape_of(stroke).specific).x2) == "6");
    CHECK(to_text(std::get<Shape::Line>(shape_of(stroke).specific).y2) == "7");

    // what the unit table does and does not convert: within a system, everything, and
    // across systems, nothing
    Node pixels = rect(Length{96.0F, Length::Unit::Px}, Length{0.0F, Length::Unit::Px});
    CHECK(moved(pixels, Length{1.0F, Length::Unit::In}, Length{0.0F, Length::Unit::Px}, problem));
    CHECK(to_text(std::get<Shape::Rect>(shape_of(pixels).specific).x) == "192px");

    Node points = rect(Length{72.0F, Length::Unit::Pt}, Length{0.0F, Length::Unit::Pt});
    CHECK(moved(points, Length{1.0F, Length::Unit::In}, Length{0.0F, Length::Unit::Pt}, problem));
    CHECK(to_text(std::get<Shape::Rect>(shape_of(points).specific).x) == "144pt");

    // an inch is exactly 25.4mm, and that is still not a sum: mixing the two systems
    // would leave the value in a system nobody chose
    Node metric = rect(mm(1), mm(1));
    CHECK(!moved(metric, Length{1.0F, Length::Unit::In}, mm(0), problem));
    CHECK(problem.find("the two systems are not mixed") != std::string::npos);

    Node relative = rect(Percentage{50.0F}, Percentage{0.0F});
    CHECK(!moved(relative, mm(10), mm(0), problem));
    CHECK(problem.find("a percentage has no sum with a length") != std::string::npos);

    Node plain = rect(user_units(1), user_units(1));
    CHECK(!moved(plain, Length{5.0F, Length::Unit::Px}, user_units(0), problem));
    CHECK(problem.find("only add within one system") != std::string::npos);

    // zero is zero in every unit: a coordinate of 0 takes an offset in any unit, and a
    // zero offset leaves any coordinate alone
    Node fresh = rect(user_units(0), user_units(0));
    CHECK(moved(fresh, Length{5.0F, Length::Unit::Px}, Length{7.0F, Length::Unit::In}, problem));
    CHECK(to_text(std::get<Shape::Rect>(shape_of(fresh).specific).x) == "5px");
    CHECK(to_text(std::get<Shape::Rect>(shape_of(fresh).specific).y) == "7in");

    Node kept = rect(Length{10.0F, Length::Unit::Mm}, Length{10.0F, Length::Unit::Mm});
    CHECK(moved(kept, user_units(0), Length{0.0F, Length::Unit::In}, problem));
    CHECK(to_text(std::get<Shape::Rect>(shape_of(kept).specific).x) == "10mm");
    CHECK(to_text(std::get<Shape::Rect>(shape_of(kept).specific).y) == "10mm");

    // a container has no coordinates, so it gets a transform instead
    Node layer = group();
    CHECK(moved(layer, user_units(2), user_units(3), problem));
    const std::optional<Transform>& first = std::get<SVGNode>(layer).transform_attr.transform;
    CHECK(first.has_value());
    CHECK(std::holds_alternative<Translate>(*first));
    CHECK(to_text(std::get<Translate>(*first).y) == "3");

    // two translates are one translate
    CHECK(moved(layer, user_units(1), user_units(1), problem));
    CHECK(to_text(std::get<Translate>(*std::get<SVGNode>(layer).transform_attr.transform).x) == "3");

    // a scale has no simple form that holds a translation next to it, so it becomes a
    // matrix with the translation in e and f
    Node scaled = group();
    std::get<SVGNode>(scaled).transform_attr.transform = Scale{2.0F, 3.0F};
    CHECK(moved(scaled, user_units(5), user_units(7), problem));
    const Matrix& matrix = std::get<Matrix>(*std::get<SVGNode>(scaled).transform_attr.transform);
    CHECK(matrix.a == 2.0F && matrix.d == 3.0F);
    CHECK(matrix.e == 5.0F && matrix.f == 7.0F);

    // a rotation becomes its cosine and sine, which is where a matrix stops being exact
    Node turned = group();
    std::get<SVGNode>(turned).transform_attr.transform = Rotate{90.0F};
    CHECK(moved(turned, user_units(1), user_units(0), problem));
    const Matrix& rotated = std::get<Matrix>(*std::get<SVGNode>(turned).transform_attr.transform);
    CHECK(std::fabs(rotated.a) < 1e-6F);
    CHECK(std::fabs(rotated.b - 1.0F) < 1e-6F);
    CHECK(std::fabs(rotated.c + 1.0F) < 1e-6F);
    CHECK(rotated.e == 1.0F);

    // a matrix counts in user units, so an offset with a unit cannot go into one
    Node inches = group();
    std::get<SVGNode>(inches).transform_attr.transform = Scale{2.0F, 2.0F};
    CHECK(!moved(inches, Length{1.0F, Length::Unit::In}, mm(0), problem));
    CHECK(problem.find("counts in user units") != std::string::npos);

    // a text node has neither coordinates nor a transform
    Node words = text_node();
    CHECK(!moved(words, user_units(1), user_units(1), problem));
    CHECK(problem.find("no coordinates and no transform") != std::string::npos);

    if (failures != 0) {
        std::printf("geometry: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("geometry: all checks passed\n");
    return 0;
}
