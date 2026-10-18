// The drawing itself: the tree a view prints, with the box characters, the one column
// its attributes line up in, and the words of a text inside the line that holds them.
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Rendering.hh"
#include "View.hh"

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::printf("FAIL line %d: %s\n", line, expression);
    ++failures;
}

LengthPercentage units(float value) { return Length{value, Length::Unit::UserUnit}; }

Node element(std::string id, std::variant<Shape, Container, Text> body) {
    SVGNode node;
    node.identity_attr = IdentityAttr{std::move(id), ""};
    node.data = std::move(body);
    return Node{std::move(node)};
}

Node rect(std::string id, float x, float y, float w, float h, std::optional<Color> fill) {
    SVGNode node = std::get<SVGNode>(element(std::move(id), Shape{Shape::Rect{units(x), units(y), units(w), units(h)}}));
    if (fill) node.paint_attr.fill = Paint{*fill};
    return Node{std::move(node)};
}

Node circle(std::string id, float cx, float cy, float r) {
    return element(std::move(id), Shape{Shape::Circle{units(cx), units(cy), units(r)}});
}

Node group(std::string id) {
    return element(std::move(id), Container{ChildAttr{}, Container::Group{}});
}

// The root element, which is where the canvas of the document lives.
Node root_with(LengthPercentage width, LengthPercentage height) {
    return element("", Container{ChildAttr{}, Container::SVG{width, height}});
}

Node text_element(std::string id, float x, float y) {
    Text text;
    text.common.x = units(x);
    text.common.y = units(y);
    text.child_attr = ChildAttr{};
    return element(std::move(id), std::move(text));
}

// One row of the view, as the Planner would hand it over: the depth, whether the node is
// its parent's last child, and the words of a text when there are any.
TreeRow row(std::size_t depth, bool last, Node payload, std::string text = {}) {
    return TreeRow{depth, last, 0, std::move(payload), std::move(text)};
}

std::string join(const std::vector<std::string>& lines) {
    std::string out;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index > 0) out += "\n";
        out += lines[index];
    }
    return out;
}

}  // namespace

#define CHECK(condition) check((condition), #condition, __LINE__)

int main() {
    // The tree of the design's own example: a rect at the back, a text, and a group with
    // two shapes in it. Every line but the first hangs from the box drawing in front of
    // it, and the attributes of all of them start in the same column - which is two
    // places past the longest name in the view, the deepest one here.
    std::vector<TreeRow> rows;
    rows.push_back(row(0, true, root_with(units(800), units(600))));
    rows.push_back(row(1, false, rect("bg", 0, 0, 800, 600, Color{Color::RGB{255, 255, 255}})));
    rows.push_back(row(1, false, text_element("title", 20, 40), "My Diagram"));
    rows.push_back(row(1, true, group("markers")));
    rows.push_back(row(2, false, circle("c1", 100, 100, 20)));
    rows.push_back(row(2, true, rect("box", 200, 80, 60, 60, Color{Color::RGB{0x33, 0x66, 0xff}})));

    const std::string expected = join({
        "svg (800x600)",
        "├── rect#bg        x=0 y=0 w=800 h=600 fill=#ffffff",
        "├── text#title     (20,40) \"My Diagram\"",
        "└── g#markers",
        "    ├── circle#c1  cx=100 cy=100 r=20",
        "    └── rect#box   x=200 y=80 w=60 h=60 fill=#3366ff",
    });
    const std::string drawing = render(TreeView{rows});
    CHECK(drawing == expected);
    if (drawing != expected) std::printf("--- drew ---\n%s\n------------\n", drawing.c_str());

    // A view rooted somewhere else draws that node first: the line under it hangs from
    // the node itself, so there is no box in front of the first one and the ones under it
    // are indented by one step.
    std::vector<TreeRow> subtree;
    subtree.push_back(row(0, true, group("markers")));
    subtree.push_back(row(1, false, circle("c1", 100, 100, 20)));
    subtree.push_back(row(1, true, rect("box", 200, 80, 60, 60, std::nullopt)));
    CHECK(render(TreeView{subtree}) == join({
                                             "g#markers",
                                             "├── circle#c1  cx=100 cy=100 r=20",
                                             "└── rect#box   x=200 y=80 w=60 h=60",
                                         }));

    // A line under an ancestor that is not the last child of its own parent hangs from a
    // bar, and the bar counts as one place even though it is more than one byte: the
    // column is where a reader sees it, not where the bytes end.
    std::vector<TreeRow> under_a_bar;
    under_a_bar.push_back(row(0, true, root_with(units(800), units(600))));
    under_a_bar.push_back(row(1, false, group("markers")));
    under_a_bar.push_back(row(2, true, rect("box", 0, 0, 10, 10, Color{Color::RGB{255, 0, 0}})));
    under_a_bar.push_back(row(1, true, text_element("title", 20, 40), "My Diagram"));
    CHECK(render(TreeView{under_a_bar}) == join({
                                                   "svg (800x600)",
                                                   "├── g#markers",
                                                   "│   └── rect#box  x=0 y=0 w=10 h=10 fill=#ff0000",
                                                   "└── text#title    (20,40) \"My Diagram\"",
                                               }));

    // A single node has a heading and one indented line per group of attributes. A node
    // with a parent says where it hangs on the heading line.
    CHECK(render(NodeView{3, rect("box", 200, 80, 60, 60, Color{Color::RGB{0x33, 0x66, 0xff}}), {}}) ==
          "rect#box\n  x=200 y=80 w=60 h=60\n  fill=#3366ff");
    CHECK(render(NodeView{3, rect("red", 0, 0, 1, 1, Color{Color::Named{Color::NamedColorId::RebeccaPurple}}), {}}) ==
          "rect#red\n  x=0 y=0 w=1 h=1\n  fill=rebeccapurple");
    CHECK(render(NodeView{3, rect("box", 200, 80, 60, 60, Color{Color::RGB{0x33, 0x66, 0xff}}), {},
                           NodeParent{1, group("markers")}}) ==
          "rect#box  parent=g#markers\n  x=200 y=80 w=60 h=60\n  fill=#3366ff");
    CHECK(render(NodeView{1, group("markers"), {}}) == "g#markers");
    CHECK(render(NodeView{1, group("markers"), {}, NodeParent{0, root_with(units(800), units(600))}}) ==
          "g#markers  parent=svg");
    CHECK(render(NodeView{4, text_element("title", 20, 40), "My Diagram"}) == "text#title (20,40) \"My Diagram\"");
    CHECK(render(NodeView{4, text_element("title", 20, 40), "My Diagram", NodeParent{1, group("markers")}}) ==
          "text#title  parent=g#markers\n  (20,40) \"My Diagram\"");

    // An element with no id of its own is named by its kind alone, which is what the root
    // of an empty document is
    CHECK(render(NodeView{0, root_with(units(800), units(600)), {}}) == "svg (800x600)");

    // A unit both sides of the canvas share is said once, and a canvas that is not a plain
    // number keeps its own text: what a length is, is what to_text says it is.
    CHECK(render(NodeView{0, root_with(Length{1024.0F, Length::Unit::Px}, Length{768.0F, Length::Unit::Px}), {}}) ==
          "svg (1024x768)px");
    CHECK(render(NodeView{0, root_with(Length{2.0F, Length::Unit::Cm}, Length{3.0F, Length::Unit::Mm}), {}}) ==
          "svg (2cmx3mm)");

    // Character data asked for by handle has no id and no kind of its own: it is the words
    // it is made of.
    CHECK(render(NodeView{7, Node{PlainText{"hello"}}, "hello"}) == "<text-node> \"hello\"");

    // The other half of the boundary: an event and a refusal are data until this file
    // turns them into words. Editor results have no route prefix; the special branches
    // keep theirs.
    CHECK(render(event::Created{2, "rect", "box", 1, "g", "markers", false}) == "rect#box created");
    CHECK(render(event::Created{3, "text", "start label", 1, "g", "node 1", true}) ==
          "text#\"start label\" created in g#\"node 1\"");
    CHECK(render(event::Moved{2, units(5), units(7)}) == "moved #2 by 5 7");
    CHECK(render(event::ContentSet{2}) == "set text on #2");
    CHECK(render(event::Deleted{5, SvgIdRef{"box"}, 0}) == "deleted box (#5)");
    CHECK(render(event::Deleted{6, SvgIdRef{"markers"}, 2}) == "deleted markers (#6) and 2 node(s) under it");
    CHECK(render(event::Initialized{units(800), units(600)}) == "canvas 800x600 created");
    CHECK(render(event::Opened{"a.svg", {{"rect1", "rect"}}}) ==
          "read a.svg\nwarning: assigned id rect1 to unnamed <rect>");
    CHECK(render(fault::DuplicateId{"r1"}) == "error: id already exists: r1");
    CHECK(render(fault::IncompatibleLengths{Length{1.0F, Length::Unit::In}, Length{1.0F, Length::Unit::Cm}}) ==
          "error: the offset 1in does not add up with 1cm: 1in is imperial and 1cm is metric - mm and cm are "
          "metric, in, pt and px are defined against the inch, and the two systems are not mixed");
    CHECK(render(Outcome{Kind::Change, Refusal{fault::UnknownAttribute{"x"}}}) ==
          "error: unknown attribute: x");
    CHECK(render(Outcome{Kind::View, Refusal{fault::MissingShape{SvgIdRef{"id with space"}}}}) ==
          "[view] error: no such shape: \"id with space\"");
    CHECK(render(Outcome{Kind::Change, Event{event::NothingMoved{}}}) ==
          "the offset is zero, nothing moved");
    CHECK(render(Outcome{Kind::Context, Event{event::Initialized{units(800), units(600)}}}) ==
          "[context] canvas 800x600 created");
    CHECK(render(Outcome{Kind::Unknown, Refusal{fault::InternalError{}}}) == "[unknown] error: internal error");

    if (failures != 0) {
        std::printf("rendering: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("rendering: all checks passed\n");
    return 0;
}
