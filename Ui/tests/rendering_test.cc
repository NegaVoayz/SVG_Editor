// The drawing itself: the tree a view prints, with the box characters, the one column
// its attributes line up in, and the words of a text inside the line that holds them.
#include <cstdio>
#include <memory>
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

ElementShape element(std::string id, ElementData body) {
    return ElementShape{IdentityAttr{std::move(id), ""}, {}, std::move(body)};
}

ElementShape rect(std::string id, float x, float y, float w, float h, std::optional<Color> fill) {
    ElementShape node =
        element(std::move(id), Shape{Shape::Rect{units(x), units(y), units(w), units(h)}});
    if (fill) node.paint_attr.fill = Paint{*fill};
    return node;
}

ElementShape circle(std::string id, float cx, float cy, float r) {
    return element(std::move(id), Shape{Shape::Circle{units(cx), units(cy), units(r)}});
}

ElementShape group(std::string id) {
    return element(std::move(id), ContainerShape{Container::Group{}});
}

ElementShape box_group(std::string id) {
    return element(std::move(id), ContainerShape{Container::Group{"box"}});
}

Transform composed_transform(std::initializer_list<Transform> steps) {
    Transform result;
    for (const Transform& step : steps) result = *compose(result, step);
    return result;
}

// The root element, which is where the canvas of the document lives.
ElementShape root_with(LengthPercentage width, LengthPercentage height) {
    return element("", ContainerShape{Container::SVG{width, height}});
}

ElementShape unsized_root_with(Container::ViewBox view_box) {
    Container::SVG canvas{std::nullopt, std::nullopt};
    canvas.view_box = view_box;
    return element("", ContainerShape{std::move(canvas)});
}

ElementShape text_element(std::string id, float x, float y) {
    TextShape text;
    text.common.x = units(x);
    text.common.y = units(y);
    text.specific = Text::Wrapper{};
    return element(std::move(id), std::move(text));
}

// One row of the view, as the Planner would hand it over: the depth, whether the node is
// its parent's last child, and the words of a text when there are any.
TreeRow row(std::size_t depth, bool last, ElementShape shape, std::string text = {}) {
    return TreeRow{depth, last, 0, std::move(shape), std::move(text)};
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
        "svg (800 x 600 user units)",
        "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 rect#bg        x=0 y=0 w=800 h=600 fill=#ffffff",
        "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 text#title     (20,40) text=\"My Diagram\"",
        "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 g#markers",
        "    \xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 circle#c1  cx=100 cy=100 r=20",
        "    \xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 rect#box   x=200 y=80 w=60 h=60 fill=#3366ff",
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
                                             "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 circle#c1  cx=100 cy=100 r=20",
                                             "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 rect#box   x=200 y=80 w=60 h=60",
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
                                                   "svg (800 x 600 user units)",
                                                   "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 g#markers",
                                                   "\xE2\x94\x82   \xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 rect#box  x=0 y=0 w=10 h=10 fill=#ff0000",
                                                   "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 text#title    (20,40) "
                                                   "text=\"My Diagram\"",
                                               }));

    // A single node has a heading and one indented line per group of attributes. A node
    // with a parent says where it hangs on the heading line.
    CHECK(render(NodeView{3, rect("box", 200, 80, 60, 60, Color{Color::RGB{0x33, 0x66, 0xff}}), {}}) ==
          "rect#box\n  x=200 y=80 w=60 h=60\n  fill=#3366ff");
    ElementShape classified = rect("tagged", 0, 0, 1, 1, std::nullopt);
    classified.identity_attr.class_ = "a b";
    CHECK(render(NodeView{3, std::move(classified), {}}) ==
          "rect#tagged\n  class=\"a b\"\n  x=0 y=0 w=1 h=1");
    CHECK(render(NodeView{3, rect("red", 0, 0, 1, 1, Color{Color::Named{Color::NamedColorId::RebeccaPurple}}), {}}) ==
          "rect#red\n  x=0 y=0 w=1 h=1\n  fill=rebeccapurple");
    CHECK(render(NodeView{3, rect("float", 1.234567F, 0, 1, 1, std::nullopt), {}}) ==
          "rect#float x=1.234567 y=0 w=1 h=1");
    CHECK(render(NodeView{3, rect("turned", 0, 0, 1, 1, std::nullopt), {}, std::nullopt,
                          composed_transform(
                              {scaled(2.0F, 2.0F), *rotation_about(30.0F, units(10), units(20)),
                               skewed_x(15.0F), skewed_y(-5.0F)})}) ==
          "rect#turned\n  x=0 y=0 w=1 h=1\n  transform=translate(22.679493 -4.641014) rotate(15) "
          "scale(1.931852 2.070552) skewY(9.23052)");
    CHECK(render(NodeView{3, rect("box", 200, 80, 60, 60, Color{Color::RGB{0x33, 0x66, 0xff}}), {},
                           NodeParent{1, group("markers")}}) ==
          "rect#box  parent=g#markers\n  x=200 y=80 w=60 h=60\n  fill=#3366ff");
    CHECK(render(NodeView{1, group("markers"), {}}) == "g#markers");
    CHECK(render(NodeView{1, box_group("status"), {}}) == "g#status (box)");
    CHECK(render(TreeView{{row(0, true, box_group("status"))}}) == "g#status (box)");
    CHECK(render(NodeView{1, group("markers"), {}, NodeParent{0, root_with(units(800), units(600))}}) ==
          "g#markers  parent=svg");
    CHECK(render(NodeView{4, text_element("title", 20, 40), "My Diagram"}) ==
          "text#title\n  (20,40)\n  text=\"My Diagram\"");
    CHECK(render(NodeView{4, text_element("title", 20, 40), "My Diagram", NodeParent{1, group("markers")}}) ==
          "text#title  parent=g#markers\n  (20,40)\n  text=\"My Diagram\"");

    // The character data is the last thing a text prints, after the paint attributes.
    ElementShape filled_text = text_element("title", 20, 40);
    filled_text.paint_attr.fill = Paint{Color{Color::Named{Color::NamedColorId::Red}}};
    CHECK(render(NodeView{4, filled_text, "My Diagram"}) ==
          "text#title\n  (20,40)\n  fill=red\n  text=\"My Diagram\"");
    CHECK(render(TreeView{{row(0, true, filled_text, "My Diagram")}}) ==
          "text#title (20,40) fill=red text=\"My Diagram\"");

    // An element with no id of its own is named by its kind alone, which is what the root
    // of an empty document is
    CHECK(render(NodeView{0, root_with(units(800), units(600)), {}}) == "svg (800 x 600 user units)");

    // A root that describes only its user-coordinate rectangle has no explicit size.
    CHECK(render(NodeView{0, unsized_root_with(Container::ViewBox{0.0F, 0.0F, 100.0F, 50.0F}), {}}) ==
          "svg (unsized) viewBox=\"0 0 100 50\"");

    // xmlns comes before the canvas and viewBox, as SvgWriter writes it before width,
    // height and viewBox.
    ElementShape namespaced = unsized_root_with(Container::ViewBox{0.0F, 0.0F, 100.0F, 50.0F});
    std::get<Container::SVG>(std::get<ContainerShape>(namespaced.data).specific).xmlns =
        "urn:example:svg";
    CHECK(render(NodeView{0, namespaced, {}}) ==
          "svg xmlns=\"urn:example:svg\" (unsized) viewBox=\"0 0 100 50\"");

    // A unit both sides share stays inside the parentheses. A partial canvas names the
    // unit on the side that is present.
    CHECK(render(NodeView{0, root_with(Length{1024.0F, Length::Unit::Px}, Length{768.0F, Length::Unit::Px}), {}}) ==
          "svg (1024 x 768 px)");
    CHECK(render(NodeView{0, root_with(Length{2.0F, Length::Unit::Cm}, Length{3.0F, Length::Unit::Mm}), {}}) ==
          "svg (2cm x 3mm)");
    CHECK(render(NodeView{0,
                          element("", ContainerShape{Container::SVG{units(300), std::nullopt}}),
                          {}}) == "svg (300 user units x unset)");
    CHECK(render(NodeView{0,
                          element("", ContainerShape{Container::SVG{std::nullopt, units(300)}}),
                          {}}) == "svg (unset x 300 user units)");

    // Character data asked for by handle has no id and no kind of its own: it is the
    // labelled words it is made of.
    CHECK(render(NodeView{7, CharacterView{"hello"}, "hello"}) == "<text-node> text=\"hello\"");

    // The history is shown newest first, with the baseline last. Entries stay
    // chronological in the log; rendering reverses them for the terminal.
    const event::HistoryEntries history_entries{
        {12, event::EditEvent{event::Created{{"rect", "r"}, {"svg", ""}, false}}, 1},
        {13, event::EditEvent{event::Moved{{"rect", "r"}, units(5), units(7)}}, 2},
    };
    const event::HistoryCursor history_cursor{11, 13, 2, 2};
    const HistoryView history{history_entries, history_cursor, 20};
    CHECK(render(history) ==
          "13  rect#r moved by (5, 7) (HEAD)\n12  rect#r created\n11  baseline");

    // Commit ids line up by their widest value, and folded commits are counted between
    // the visible checkpoints rather than reported as one global total.
    const event::HistoryEntries capped_entries{
        {9, event::EditEvent{event::Created{{"rect", "old"}, {"svg", ""}, false}}, 1},
        {10, event::EditEvent{event::Created{{"rect", "a"}, {"svg", ""}, false}}, 2},
        {11, event::EditEvent{event::Moved{{"rect", "a"}, units(1), units(2)}}, 3},
    };
    const event::HistoryCursor capped_cursor{8, 11, 3, 3};
    const HistoryView capped{capped_entries, capped_cursor, 1};
    CHECK(render(capped) ==
          "11  rect#a moved by (1, 2) (HEAD)\n10  rect#a created\n"
          "... 1 commit folded ...\n 8  baseline");

    // The baseline, HEAD, and newest commit are anchors even when the cap is smaller than
    // the range between them. Each omitted range gets its own folded count.
    const event::HistoryEntries anchored_entries{
        {11, event::EditEvent{event::Created{{"rect", "old"}, {"svg", ""}, false}}, 1},
        {12, event::EditEvent{event::Created{{"rect", "near"}, {"svg", ""}, false}}, 2},
        {13, event::EditEvent{event::Created{{"rect", "head"}, {"svg", ""}, false}}, 3},
        {14, event::EditEvent{event::Moved{{"rect", "head"}, units(0), units(0)}}, 4},
        {15, event::EditEvent{event::Moved{{"rect", "head"}, units(1), units(0)}}, 5},
    };
    const event::HistoryCursor anchored_cursor{10, 13, 3, 5};
    const HistoryView anchored{anchored_entries, anchored_cursor, 1};
    CHECK(render(anchored) ==
          "15  rect#head moved by (1, 0)\n... 1 commit folded ...\n"
          "13  rect#head created (HEAD)\n12  rect#near created\n"
          "... 1 commit folded ...\n10  baseline");
    const event::HistoryEntries empty_entries;
    const event::HistoryCursor empty_cursor;
    CHECK(render(HistoryView{empty_entries, empty_cursor, 20}) == "no history");

    // The other half of the boundary: an event and a refusal are data until this file
    // turns them into words. Every branch is plain prose; a refusal starts with `error:`.
    CHECK(render(event::Created{{"rect", "box"}, {"g", "markers"}, false}) == "rect#box created");
    CHECK(render(event::Created{{"text", "start label"}, {"g", "node 1"}, true}) ==
          "text#\"start label\" created in g#\"node 1\"");
    CHECK(render(event::Grouped{{"g", "markers"}, {"svg", ""}, 2}) ==
          "g#markers created (2 elements)");
    CHECK(render(event::Ungrouped{{"g", "pair"}, {"svg", ""}, 2}) ==
          "g#pair ungrouped into svg (2 elements)");
    CHECK(render(event::Moved{{"rect", "r1"}, units(5), units(7)}) == "rect#r1 moved by (5, 7)");
    CHECK(render(event::Moved{{"rect", "r1"}, units(5), units(7), EditMode::Transform}) ==
          "rect#r1 moved by (5, 7)");
    CHECK(render(event::Moved{{"g", "g1"}, units(5), units(7), EditMode::Cascade, 3}) ==
          "g#g1 moved by (5, 7) (3 elements)");
    CHECK(render(event::Moved{{"g", "node2"}, units(0), units(100), EditMode::ContainerTransform}) ==
          "g#node2 moved by (0, 100)");
    CHECK(render(event::Moved{{"g", "node2"}, units(0), units(100), EditMode::Transform}) ==
          "g#node2 moved by (0, 100)");
    CHECK(render(event::Scaled{{"rect", "r1"}, 2.0F}) == "rect#r1 scaled by 2");
    CHECK(render(event::Scaled{{"rect", "r1"}, 2.0F, EditMode::Transform}) == "rect#r1 scaled by 2");
    CHECK(render(event::Scaled{{"g", "g1"}, 2.0F, EditMode::Cascade, 2}) ==
          "g#g1 scaled by 2 (2 elements)");
    CHECK(render(event::Scaled{{"g", "node2"}, 3.0F, EditMode::ContainerTransform}) ==
          "g#node2 scaled by 3");
    CHECK(render(event::AddedTo{{"rect", "r1"}, {"g", "g1"}}) == "rect#r1 moved to the end of g#g1");
    CHECK(render(event::MovedToFront{{"rect", "r1"}, {"g", "g1"}}) ==
          "rect#r1 moved to the front of g#g1");
    CHECK(render(event::MovedToBack{{"rect", "r1"}, {"g", "g1"}}) ==
          "rect#r1 moved to the back of g#g1");
    CHECK(render(event::AttributeSet{{"rect", "r1"}, "fill", "red"}) == "rect#r1 updated: fill=red");
    CHECK(render(event::AttributeSet{{"rect", "r1"}, "class", "layer one"}) ==
          "rect#r1 updated: class=\"layer one\"");
    CHECK(render(event::ContentSet{{"text", "title"}, "Hello World"}) ==
          "text#title updated: text=\"Hello World\"");
    CHECK(render(event::ContentSet{{"text", "title"}, "a \"q\" b\\c"}) ==
          "text#title updated: text=\"a \\\"q\\\" b\\\\c\"");
    CHECK(render(event::CanvasSet{units(300), std::nullopt}) ==
          "the canvas is now 300 x unset, and the history starts from here");
    CHECK(render(event::Deleted{{"rect", "box"}, 0}) == "rect#box deleted");
    CHECK(render(event::Deleted{{"g", "markers"}, 2}) == "g#markers deleted (2 elements)");
    CHECK(render(event::Undone{
              3, 3, {},
              std::make_shared<const event::EditEvent>(
                  event::Moved{{"rect", "r1"}, units(5), units(7)}),
              12}) ==
          "undo: rect#r1 moved by (5, 7) (HEAD 12)");
    CHECK(render(event::Redone{
              3, 3, {},
              std::make_shared<const event::EditEvent>(
                  event::Moved{{"rect", "r1"}, units(5), units(7)}),
              13}) ==
          "redo: rect#r1 moved by (5, 7) (HEAD 13)");
    const event::HistoryEntries timeline_entries{
        {12, event::EditEvent{event::Created{{"rect", "r1"}, {"svg", ""}, false}}, 1},
        {13, event::EditEvent{event::Moved{{"rect", "r1"}, units(5), units(7)}}, 2},
    };
    const event::HistoryCursor timeline_cursor{11, 13, 2, 2};
    const HistoryView timeline{timeline_entries, timeline_cursor, 20};
    CHECK(render(event::CheckedOut{2, 2, {}, 13, 11, timeline, false}) ==
          "checked out 13\n13  rect#r1 moved by (5, 7) (HEAD)\n12  rect#r1 created\n11  baseline");
    CHECK(render(event::CheckedOut{2, 2, {}, 13, 11, timeline, true}) ==
          "checked out 13 (no effect)\n13  rect#r1 moved by (5, 7) (HEAD)\n"
          "12  rect#r1 created\n11  baseline");
    const event::HistoryCursor empty_timeline_cursor{11, 11, 0, 0};
    const HistoryView empty_timeline{empty_entries, empty_timeline_cursor, 20};
    CHECK(render(event::CheckedOut{0, 0, {}, 11, 11, empty_timeline, true}) ==
          "checked out 11 (no effect)\n11  baseline (HEAD)");
    CHECK(render(event::AlreadyFront{{"rect", "box"}}) == "rect#box is already in front");
    CHECK(render(event::AlreadyBack{{"rect", "box"}}) == "rect#box is already at the back");
    CHECK(render(event::Initialized{units(800), units(600)}) == "canvas 800x600 created");
    CHECK(render(event::Opened{"a.svg", {{"rect1", "rect", ""}}, 3}) ==
          "read a.svg (3 elements)\nwarning: assigned id rect1 to unnamed <rect>");
    CHECK(render(event::Opened{"a.svg", {{"rect2", "rect", "svg"}}, 3}) ==
          "read a.svg (3 elements)\nwarning: id \"svg\" is reserved for the root, renamed <rect> to rect2");
    CHECK(render(event::Saved{"demo.svg", 5}) == "saved to demo.svg (5 elements)");
    CHECK(render(event::Saved{"one.svg", 1}) == "saved to one.svg (1 element)");
    CHECK(render(fault::CannotUngroupNonContainer{SvgIdRef{"n1"}}) ==
          "error: cannot ungroup a shape or text node: n1");
    CHECK(render(fault::CannotUngroupComposite{SvgIdRef{"n1"}}) ==
          "error: cannot ungroup composite shape: n1");
    CHECK(render(fault::CannotAddToOwnDescendant{SvgIdRef{"layer"}}) ==
          "error: cannot add a container to its own descendant: layer");
    CHECK(render(fault::DuplicateId{"r1"}) == "error: id already exists: r1");
    CHECK(render(fault::NegativeDimension{"width"}) == "error: width must not be negative");
    CHECK(render(fault::NonPositiveCanvas{"height"}) == "error: canvas height must be positive");
    CHECK(render(fault::NonFiniteValue{"scaled value"}) ==
          "error: \"scaled value\" must be a finite number");
    CHECK(render(fault::UnknownCommit{42}) == "error: no such commit: 42");
    CHECK(render(fault::InvalidId{"a b"}) == "error: invalid XML id: \"a b\"");
    CHECK(render(fault::ReservedId{"svg"}) == "error: id \"svg\" is reserved for the root");
    CHECK(render(fault::TransformHasNoScope{}) == "error: transform does not take cascade or restrict");
    CHECK(render(fault::EmptyContainer{"g", "node2"}) ==
          "error: cannot move or scale an empty group: g#node2");
    CHECK(render(fault::EmptyContainer{"defs", "resources"}) ==
          "error: cannot move or scale an empty <defs> container: defs#resources");
    CHECK(render(fault::NoDirectParams{}) == "error: this node has no direct params");
    CHECK(render(fault::UndoAfterIo{}) == "error: undo not allowed after io command");
    CHECK(render(fault::IncompatibleLengths{Length{1.0F, Length::Unit::In}, Length{1.0F, Length::Unit::Cm}}) ==
          "error: the offset 1in does not add up with 1cm: 1in is imperial and 1cm is metric - mm and cm are "
          "metric, in, pt and px are defined against the inch, and the two systems are not mixed");
    CHECK(render(Outcome{Kind::Change, Refusal{fault::UnknownAttribute{"x"}}}) ==
          "error: unknown attribute: x");
    CHECK(render(Outcome{Kind::View, Refusal{fault::MissingShape{SvgIdRef{"id with space"}}}}) ==
          "error: no such shape: \"id with space\"");
    CHECK(render(Outcome{Kind::Change, Event{event::NothingMoved{}}}) ==
          "the offset is zero, nothing moved");
    CHECK(render(Outcome{Kind::Context, Event{event::Initialized{units(800), units(600)}}}) ==
          "canvas 800x600 created");
    CHECK(render(Outcome{Kind::Unknown, Refusal{fault::InternalError{}}}) == "error: internal error");

    if (failures != 0) {
        std::printf("rendering: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("rendering: all checks passed\n");
    return 0;
}
