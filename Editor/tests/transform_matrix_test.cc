// Transform storage: a node carries one affine matrix, an edit pre-multiplies it, and undo
// only changes the node record back to the matrix it held before.
#include <cmath>
#include <cstdio>
#include <optional>

#include "Document.hh"
#include "SVGNode.hh"

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::printf("FAIL line %d: %s\n", line, expression);
    ++failures;
}

LengthPercentage user_units(float value) { return Length{value, Length::Unit::UserUnit}; }

Node group() {
    SVGNode element;
    element.identity_attr = IdentityAttr{"g", ""};
    element.data = Container{ChildAttr{}, Container::Group{}};
    return Node{std::move(element)};
}

void set_transform(Document& document, Id node, const Transform& transform) {
    std::optional<Node> payload = document.node(node);
    std::get<SVGNode>(*payload).transform_attr.transform = transform;
    Transaction edit = document.transaction();
    edit.update(node, std::move(*payload));
    edit.commit();
}

bool near(float left, float right) { return std::fabs(left - right) <= 1.0e-5F; }

}  // namespace

#define CHECK(condition) check((condition), #condition, __LINE__)

int main() {
    Document document;
    document.reset(std::nullopt, std::nullopt);

    Id layer = no_id;
    {
        Transaction edit = document.transaction();
        layer = edit.append_child(document.root(), group());
        edit.commit();
    }

    CHECK(is_identity(document.transform(layer)));

    set_transform(document, layer, translated(user_units(2), user_units(3)));
    CHECK(transform_text(document.transform(layer)) == "translate(2 3)");

    // A second move is composed into the matrix, so two translations become one total.
    set_transform(document, layer, *compose(translated(user_units(1), user_units(1)), document.transform(layer)));
    CHECK(transform_text(document.transform(layer)) == "translate(3 4)");

    // A scale in front of the translation scales the translation too, as
    // scale(2) translate(...) means in SVG.
    set_transform(document, layer, *compose(scaled(2.0F, 2.0F), document.transform(layer)));
    CHECK(transform_text(document.transform(layer)) == "translate(6 8) scale(2 2)");

    // Undo moves the node back to the older matrix; redo finds the newer one again.
    document.undo();
    CHECK(transform_text(document.transform(layer)) == "translate(3 4)");
    document.redo();
    CHECK(transform_text(document.transform(layer)) == "translate(6 8) scale(2 2)");

    // A whole list is composed into one matrix, including repeated kinds and a rotate centre.
    Transform written;
    for (const Transform& step :
         {scaled(2.0F, 2.0F), *rotation_about(30.0F, user_units(10), user_units(20)), skewed_x(15.0F),
          skewed_y(-5.0F), rotated(45.0F)}) {
        written = *compose(written, step);
    }
    set_transform(document, layer, written);
    CHECK(!is_identity(document.transform(layer)));
    CHECK(near(document.transform(layer).a, 0.8789602F));
    CHECK(near(document.transform(layer).b, 1.9975927F));
    CHECK(near(document.transform(layer).c, -1.6368350F));
    CHECK(near(document.transform(layer).d, 0.8308344F));

    // A rolled-back edit leaves the matrix alone.
    {
        Transaction edit = document.transaction();
        std::optional<Node> payload = document.node(layer);
        std::get<SVGNode>(*payload).transform_attr.transform =
            *compose(translated(user_units(9), user_units(9)), written);
        edit.update(layer, std::move(*payload));
    }
    CHECK(near(document.transform(layer).a, 0.8789602F));

    set_transform(document, layer, Transform{});
    CHECK(is_identity(document.transform(layer)));

    // Both decompositions are exact. The shorter rendering wins, so a skew stays a single
    // skew step instead of expanding into a rotate/scale/skew form with the other axis.
    const TransformParts skew_x = decomposed_with_x(skewed_x(15.0F));
    const TransformParts skew_y = decomposed_with_y(skewed_x(15.0F));
    CHECK(skew_x.skew_axis == SkewAxis::X);
    CHECK(skew_y.skew_axis == SkewAxis::Y);
    CHECK(std::fabs(skew_y.skew) < std::fabs(skew_x.skew));
    CHECK(decomposed(skewed_x(15.0F)).skew_axis == SkewAxis::X);
    CHECK(transform_text(skewed_x(15.0F)) == "skewX(15)");
    CHECK(decomposed(skewed_y(-5.0F)).skew_axis == SkewAxis::Y);
    CHECK(transform_text(skewed_y(-5.0F)) == "skewY(-5)");
    CHECK(transform_text(skewed_y(15.0F)) == "skewY(15)");

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::puts("transform matrix tests passed");
    return 0;
}
