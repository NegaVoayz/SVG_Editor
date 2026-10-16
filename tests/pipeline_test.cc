// The pipeline end to end, from the side a front end drives it: a structured
// instruction goes in at the Planner, and a view, a change or a context change comes
// back out. A view comes back as data - turning it into words is the front end's job,
// which the cli tests cover - and parsing is not in here either.
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "Document.hh"
#include "Editor.hh"
#include "Planner.hh"
#include "Protocol.hh"

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::printf("FAIL line %d: %s\n", line, expression);
    ++failures;
}

LengthPercentage units(float value) { return Length{value, Length::Unit::UserUnit}; }

std::optional<Shape::Rect> rect_of(const Node& payload) {
    if (!std::holds_alternative<SVGNode>(payload)) return std::nullopt;
    const SVGNode& element = std::get<SVGNode>(payload);
    if (!std::holds_alternative<Shape>(element.data)) return std::nullopt;
    const Shape& shape = std::get<Shape>(element.data);
    if (!std::holds_alternative<Shape::Rect>(shape.specific)) return std::nullopt;
    return std::get<Shape::Rect>(shape.specific);
}

std::optional<Transform> transform_of(const Node& payload) {
    if (!std::holds_alternative<SVGNode>(payload)) return std::nullopt;
    return std::get<SVGNode>(payload).transform_attr.transform;
}

// The row of one element of a tree view, found by its SVG id.
std::optional<Node> node_in(const Outcome& outcome, std::string_view svg_id) {
    if (!std::holds_alternative<TreeView>(outcome.view)) return std::nullopt;
    for (const TreeRow& row : std::get<TreeView>(outcome.view).rows) {
        if (!std::holds_alternative<SVGNode>(row.payload)) continue;
        if (std::get<SVGNode>(row.payload).identity_attr.id == svg_id) return row.payload;
    }
    return std::nullopt;
}

std::optional<Color> fill_of(const Node& payload) {
    if (!std::holds_alternative<SVGNode>(payload)) return std::nullopt;
    const std::optional<Paint>& paint = std::get<SVGNode>(payload).paint_attr.fill;
    if (!paint || !std::holds_alternative<Color>(*paint)) return std::nullopt;
    return std::get<Color>(*paint);
}

}  // namespace

#define CHECK(condition) check((condition), #condition, __LINE__)

int main() {
    Document document;
    Editor editor{document};
    Planner planner{document, editor};

    // a view is served by reading the tree and hands back what it read
    const Outcome empty_view = planner.dispatch(ShowTree{});
    CHECK(empty_view.ok);
    CHECK(empty_view.kind == Kind::View);
    CHECK(std::holds_alternative<TreeView>(empty_view.view));
    if (std::holds_alternative<TreeView>(empty_view.view)) {
        const TreeView& view = std::get<TreeView>(empty_view.view);
        CHECK(view.rows.size() == 1);
        CHECK(view.rows[0].depth == 0);
        CHECK(view.rows[0].node == document.root());
    }

    // a change goes through the Editor, and one instruction is one transaction
    const Outcome created = planner.dispatch(CreateGroup{"layer"});
    CHECK(created.ok);
    CHECK(created.kind == Kind::Change);
    CHECK(created.message.find("created") != std::string::npos);
    CHECK(document.children(document.root()).size() == 1);
    CHECK(document.find("layer").has_value());

    const Outcome shape = planner.dispatch(
        CreateRect{"box", units(0), units(0), units(10), units(10), Color{Color::RGB{255, 0, 0}}});
    CHECK(shape.ok);
    CHECK(document.children(document.root()).size() == 2);

    // addto re-parents the shape, and move only changes its geometry
    const Outcome attached = planner.dispatch(AddTo{SvgIdRef{"layer"}, SvgIdRef{"box"}});
    CHECK(attached.ok);
    const Outcome moved = planner.dispatch(Move{SvgIdRef{"box"}, units(5), units(5)});
    CHECK(moved.ok);

    const Outcome found = planner.dispatch(Show{SvgIdRef{"layer"}});
    CHECK(found.ok);
    CHECK(found.kind == Kind::View);
    CHECK(std::holds_alternative<NodeView>(found.view));
    if (std::holds_alternative<NodeView>(found.view)) {
        CHECK(std::get<NodeView>(found.view).node == document.find("layer"));
    }
    CHECK(!planner.dispatch(Show{SvgIdRef{"nothing"}}).ok);

    // the tree view is the tree: a row per node, with its depth and its payload
    const Outcome tree = planner.dispatch(ShowTree{});
    CHECK(tree.ok);
    CHECK(std::holds_alternative<TreeView>(tree.view));
    if (std::holds_alternative<TreeView>(tree.view)) {
        const std::vector<TreeRow>& rows = std::get<TreeView>(tree.view).rows;
        CHECK(rows.size() == 3);
        CHECK(rows[0].node == document.root());
        CHECK(rows[1].node == document.find("layer"));
        CHECK(rows[1].depth == 1);
        CHECK(rows[2].node == document.find("box"));
        CHECK(rows[2].depth == 2);
        // the drawing needs to know which rows are the last child of their parent
        CHECK(rows[1].last);
        CHECK(rows[2].last);
    }

    // and a subtree view is rooted where it was asked to be
    const Outcome inside_layer = planner.dispatch(ShowTree{SvgIdRef{"layer"}});
    CHECK(inside_layer.ok);
    if (std::holds_alternative<TreeView>(inside_layer.view)) {
        const std::vector<TreeRow>& rows = std::get<TreeView>(inside_layer.view).rows;
        CHECK(rows.size() == 2);
        CHECK(rows[0].node == document.find("layer"));
        CHECK(rows[0].depth == 0);
    }
    CHECK(!planner.dispatch(ShowTree{SvgIdRef{"nothing"}}).ok);

    const std::optional<Node> box_row = node_in(tree, "box");
    CHECK(box_row.has_value());
    if (box_row) {
        const std::optional<Shape::Rect> geometry = rect_of(*box_row);
        CHECK(geometry.has_value());
        if (geometry) {
            CHECK(to_text(geometry->x) == "5");
            CHECK(to_text(geometry->w) == "10");
        }
        const std::optional<Color> fill = fill_of(*box_row);
        CHECK(fill.has_value());
        if (fill) CHECK(std::holds_alternative<Color::RGB>(fill->specific));
    }

    // One undo step is one instruction, in order: the move first, then the addto that
    // put the shape under the group, then the create that made it.
    CHECK(planner.dispatch(Undo{}).ok);
    CHECK(planner.dispatch(Undo{}).ok);
    CHECK(document.children(document.root()).size() == 2);
    CHECK(planner.dispatch(Undo{}).ok);
    CHECK(document.find("box") == std::nullopt);
    CHECK(document.children(document.root()).size() == 1);

    CHECK(planner.dispatch(Redo{}).ok);
    CHECK(document.find("layer").has_value());
    CHECK(document.find("box").has_value());
    CHECK(document.children(document.root()).size() == 2);

    // a context change is the Model's file layer, and it reports that there is no
    // parser yet instead of pretending it opened something
    const Outcome missing = planner.dispatch(Read{"no-such-file.svg"});
    CHECK(!missing.ok);
    CHECK(missing.kind == Kind::Context);
    CHECK(missing.message.find("cannot open") != std::string::npos);

    const Outcome unparsed = planner.dispatch(Read{std::string(SVG_EDITOR_SOURCE_DIR) + "/Structure.md"});
    CHECK(!unparsed.ok);
    CHECK(unparsed.kind == Kind::Context);
    CHECK(unparsed.message.find("not implemented") != std::string::npos);

    // A reference that names nothing is refused by the layer that owns the write path,
    // and the document is left exactly as it was.
    const Outcome bad_handle = planner.dispatch(Move{HandleRef{9999}, units(1), units(1)});
    CHECK(!bad_handle.ok);
    CHECK(bad_handle.kind == Kind::Change);
    CHECK(bad_handle.message.find("no node lives at handle #9999") != std::string::npos);

    const Outcome bad_name = planner.dispatch(Delete{SvgIdRef{"nothing"}});
    CHECK(!bad_name.ok);
    CHECK(bad_name.message.find("no node is called \"nothing\"") != std::string::npos);

    // and so is what the model refuses to do with a node that does exist
    const Outcome not_a_container = planner.dispatch(AddTo{SvgIdRef{"box"}, SvgIdRef{"layer"}});
    CHECK(!not_a_container.ok);
    CHECK(not_a_container.message.find("cannot hold children") != std::string::npos);
    CHECK(document.children(document.root()).size() == 2);

    const Outcome rootless = planner.dispatch(Delete{RootRef{}});
    CHECK(!rootless.ok);
    CHECK(rootless.message.find("root cannot be erased") != std::string::npos);
    CHECK(document.find("layer").has_value());

    // an id that is taken is refused, and one that is free is accepted
    const Outcome taken = planner.dispatch(CreateGroup{"box"});
    CHECK(!taken.ok);
    CHECK(taken.message.find("already called") != std::string::npos);
    CHECK(planner.dispatch(CreateGroup{"other"}).ok);

    // Moving a container is its transform, because a group has no coordinates of its
    // own: the first move becomes a translate and a later one extends it.
    CHECK(planner.dispatch(Move{SvgIdRef{"layer"}, units(3), units(4)}).ok);
    CHECK(planner.dispatch(Move{SvgIdRef{"layer"}, units(1), units(1)}).ok);
    const Outcome after_moves = planner.dispatch(ShowTree{});
    const std::optional<Node> moved_layer = node_in(after_moves, "layer");
    CHECK(moved_layer.has_value());
    if (moved_layer) {
        const std::optional<Transform> transform = transform_of(*moved_layer);
        CHECK(transform.has_value());
        if (transform) {
            CHECK(std::holds_alternative<Translate>(*transform));
            CHECK(to_text(std::get<Translate>(*transform).x) == "4");
            CHECK(to_text(std::get<Translate>(*transform).y) == "5");
        }
    }

    CHECK(planner.dispatch(Undo{}).ok);
    const Outcome after_undo = planner.dispatch(ShowTree{});
    const std::optional<Node> once_moved = node_in(after_undo, "layer");
    CHECK(once_moved.has_value());
    if (once_moved) {
        const std::optional<Transform> transform = transform_of(*once_moved);
        if (transform) CHECK(to_text(std::get<Translate>(*transform).x) == "3");
    }
    CHECK(planner.dispatch(Undo{}).ok);
    const Outcome after_both = planner.dispatch(ShowTree{});
    const std::optional<Node> never_moved = node_in(after_both, "layer");
    CHECK(never_moved.has_value());
    if (never_moved) CHECK(!transform_of(*never_moved).has_value());

    // Units: mm and cm are one system and convert, in the unit the value was written
    // in; inches are the other system and are not mixed into it.
    const Outcome ruler = planner.dispatch(CreateRect{"ruler", Length{2.0F, Length::Unit::Cm},
                                                      Length{0.0F, Length::Unit::Mm},
                                                      Length{10.0F, Length::Unit::Mm},
                                                      Length{10.0F, Length::Unit::Mm}, std::nullopt});
    CHECK(ruler.ok);
    CHECK(planner.dispatch(Move{SvgIdRef{"ruler"}, Length{5.0F, Length::Unit::Mm}, Length{0.0F, Length::Unit::Mm}})
              .ok);  // 2cm + 5mm = 2.5cm
    const Outcome inch_into_metric =
        planner.dispatch(Move{SvgIdRef{"ruler"}, Length{1.0F, Length::Unit::In}, Length{0.0F, Length::Unit::Mm}});
    CHECK(!inch_into_metric.ok);
    CHECK(inch_into_metric.message.find("the two systems are not mixed") != std::string::npos);

    const Outcome converted = planner.dispatch(ShowTree{});
    const std::optional<Node> ruler_row = node_in(converted, "ruler");
    CHECK(ruler_row.has_value());
    if (ruler_row) {
        const std::optional<Shape::Rect> geometry = rect_of(*ruler_row);
        CHECK(geometry.has_value());
        if (geometry) {
            const Length& x = std::get<Length>(geometry->x);
            CHECK(x.unit == Length::Unit::Cm);
            CHECK(std::fabs(x.value - 2.5F) < 0.001F);
        }
    }

    // A user unit is not a px: its size is whatever the viewport says, so that pair is
    // refused even though one of the two is a plain number.
    CHECK(planner.dispatch(Move{SvgIdRef{"box"}, units(5), units(0)}).ok);
    const Outcome px_into_user_unit =
        planner.dispatch(Move{SvgIdRef{"box"}, Length{1.0F, Length::Unit::Px}, units(0)});
    CHECK(!px_into_user_unit.ok);
    CHECK(px_into_user_unit.message.find("1px is imperial") != std::string::npos);

    const Outcome relative = planner.dispatch(CreateRect{"relative", Percentage{50.0F}, Percentage{0.0F},
                                                         units(1), units(1), std::nullopt});
    CHECK(relative.ok);
    const Outcome into_percentage = planner.dispatch(Move{SvgIdRef{"relative"}, units(1), units(0)});
    CHECK(!into_percentage.ok);
    CHECK(into_percentage.message.find("a percentage has no sum with a length") != std::string::npos);

    // --- the rest of the editing vocabulary ---

    const std::optional<Id> layer = document.find("layer");

    // A create says where the element goes, so a tree is built where it belongs
    // rather than moved there afterwards.
    const Outcome round = planner.dispatch(CreateCircle{"c1", units(10), units(10), units(5),
                                                        Color{Color::RGB{0, 0, 255}}, SvgIdRef{"layer"}});
    CHECK(round.ok);
    const std::optional<Id> dot = document.find("c1");
    CHECK(dot.has_value());
    if (dot && layer) CHECK(document.parent(*dot) == layer);

    // a text element and the character data inside it are two nodes, and one
    // instruction is still one transaction
    CHECK(planner.dispatch(CreateText{"title", units(20), units(40), "hello world"}).ok);
    const std::optional<Id> title = document.find("title");
    CHECK(title.has_value());
    if (title) {
        const std::vector<Id> inside = document.children(*title);
        CHECK(inside.size() == 1);
        if (inside.size() == 1) {
            const std::optional<Node> content = document.node(inside.front());
            CHECK(content.has_value());
            if (content) {
                CHECK(std::holds_alternative<PlainText>(*content));
                if (std::holds_alternative<PlainText>(*content)) {
                    CHECK(std::get<PlainText>(*content).content == "hello world");
                }
            }
        }
    }

    // a line has no inside to fill, so the one colour it is given is a stroke
    CHECK(planner.dispatch(CreateLine{"edge", units(0), units(0), units(10), units(10),
                                      Color{Color::RGB{255, 0, 0}}})
              .ok);
    if (const std::optional<Id> edge = document.find("edge")) {
        const std::optional<Node> payload = document.node(*edge);
        CHECK(payload.has_value());
        if (payload) {
            const SVGNode& line_element = std::get<SVGNode>(*payload);
            CHECK(line_element.paint_attr.stroke.has_value());
            CHECK(!line_element.paint_attr.fill.has_value());
        }
    }

    // an ellipse is its own kind, and its radii are its own values
    CHECK(planner.dispatch(CreateEllipse{"oval", units(0), units(0), units(30), units(10), std::nullopt}).ok);
    CHECK(document.find("oval").has_value());

    // A scale of a container is its transform: the group had no transform left after
    // the two moves above were undone, so this is where a plain scale lands.
    CHECK(planner.dispatch(ScaleBy{SvgIdRef{"layer"}, 2.0F}).ok);
    const Outcome after_scale = planner.dispatch(ShowTree{});
    const std::optional<Node> grown_layer = node_in(after_scale, "layer");
    CHECK(grown_layer.has_value());
    if (grown_layer) {
        const std::optional<Transform> transform = transform_of(*grown_layer);
        CHECK(transform.has_value());
        if (transform) {
            CHECK(std::holds_alternative<Scale>(*transform));
            if (std::holds_alternative<Scale>(*transform)) {
                CHECK(std::get<Scale>(*transform).x == 2.0F);
                CHECK(std::get<Scale>(*transform).y == 2.0F);
            }
        }
    }
    CHECK(planner.dispatch(Undo{}).ok);
    {
        const Outcome back = planner.dispatch(ShowTree{});
        const std::optional<Node> plain_layer = node_in(back, "layer");
        CHECK(plain_layer.has_value());
        if (plain_layer) CHECK(!transform_of(*plain_layer).has_value());
    }

    // A scale of a shape is its own values, and the whole instruction is one undo
    if (dot) {
        CHECK(planner.dispatch(ScaleBy{SvgIdRef{"c1"}, 3.0F}).ok);
        const Outcome after_growth = planner.dispatch(ShowTree{});
        const std::optional<Node> grown_dot = node_in(after_growth, "c1");
        CHECK(grown_dot.has_value());
        if (grown_dot && std::holds_alternative<SVGNode>(*grown_dot)) {
            const Shape& shape = std::get<Shape>(std::get<SVGNode>(*grown_dot).data);
            CHECK(std::holds_alternative<Shape::Circle>(shape.specific));
            if (std::holds_alternative<Shape::Circle>(shape.specific)) {
                const Shape::Circle& geometry = std::get<Shape::Circle>(shape.specific);
                CHECK(to_text(geometry.r) == "15");
                CHECK(to_text(geometry.cx) == "30");
            }
        }
        CHECK(planner.dispatch(Undo{}).ok);
    }

    // A group takes the place of the first node it takes - standing where that node
    // stood, in front of the first sibling after it that is not taken itself - and
    // holds the rest in the order the command wrote them.
    const std::optional<Id> box_node = document.find("box");
    const std::optional<Id> ruler_node = document.find("ruler");
    CHECK(box_node.has_value());
    CHECK(ruler_node.has_value());
    std::optional<Id> level;  // the tree the two of them stand in
    if (box_node) level = document.parent(*box_node);
    CHECK(level.has_value());
    if (box_node && ruler_node && level) {
        const std::vector<Id> before = document.children(*level);
        CHECK(planner.dispatch(Group{"pair", {SvgIdRef{"box"}, SvgIdRef{"ruler"}}}).ok);
        const std::optional<Id> pair = document.find("pair");
        CHECK(pair.has_value());
        if (pair) {
            CHECK(document.parent(*pair) == level);
            CHECK(document.children(*pair) == std::vector<Id>({*box_node, *ruler_node}));
            CHECK(document.parent(*box_node) == pair);
            CHECK(document.parent(*ruler_node) == pair);

            // the group stands in the place the first node had, and the nodes that
            // were not taken keep their order around it
            std::vector<Id> with_group;
            for (const Id sibling : before) {
                if (sibling == *box_node) {
                    with_group.push_back(*pair);
                    continue;
                }
                if (sibling == *ruler_node) continue;
                with_group.push_back(sibling);
            }
            CHECK(document.children(*level) == with_group);

            // ungroup gives the members back the group's place, keeping their own
            // order, and the node that stood after the group still stands after them
            CHECK(planner.dispatch(Ungroup{SvgIdRef{"pair"}}).ok);
            CHECK(document.find("pair") == std::nullopt);
            std::vector<Id> released;
            for (const Id sibling : with_group) {
                if (sibling == *pair) {
                    released.push_back(*box_node);
                    released.push_back(*ruler_node);
                    continue;
                }
                released.push_back(sibling);
            }
            CHECK(document.children(*level) == released);
        }
    }

    // `back` and `front` are the two ends of the child list: first child is front
    if (box_node && level) {
        CHECK(planner.dispatch(Back{SvgIdRef{"box"}}).ok);
        CHECK(document.children(*level).back() == *box_node);
        CHECK(planner.dispatch(Front{SvgIdRef{"box"}}).ok);
        CHECK(document.children(*level).front() == *box_node);
    }
    const Outcome rootless_order = planner.dispatch(Back{RootRef{}});
    CHECK(!rootless_order.ok);
    CHECK(rootless_order.message.find("no siblings") != std::string::npos);

    // A group of nothing, and a name that is taken, are both refused
    CHECK(!planner.dispatch(Group{"empty", {}}).ok);
    CHECK(!planner.dispatch(Group{"box", {SvgIdRef{"box"}}}).ok);
    CHECK(!planner.dispatch(CreateCircle{"box", units(0), units(0), units(1), std::nullopt}).ok);

    // The canvas is the root element's own size, and setting it is a checkpoint: the
    // frame behind the drawing is what the document is now, so nothing before it can
    // be undone.
    CHECK(planner.dispatch(SetCanvas{Length{1024.0F, Length::Unit::UserUnit}, std::nullopt}).ok);
    CHECK(document.depth() == 0);
    CHECK(!document.can_undo());
    const std::optional<Node> root_payload = document.node(document.root());
    CHECK(root_payload.has_value());
    if (root_payload && std::holds_alternative<SVGNode>(*root_payload)) {
        const Container& container = std::get<Container>(std::get<SVGNode>(*root_payload).data);
        CHECK(std::holds_alternative<Container::SVG>(container.specific));
        if (std::holds_alternative<Container::SVG>(container.specific)) {
            const Container::SVG& canvas = std::get<Container::SVG>(container.specific);
            CHECK(canvas.width.has_value() && canvas.height.has_value());
            if (canvas.width) CHECK(to_text(*canvas.width) == "1024");
            if (canvas.height) CHECK(to_text(*canvas.height) == "600");
        }
    }

    // `save` writes where the document was read from unless it is given a path of its
    // own, and a document that came from nowhere has no such path: that, and not the
    // missing serializer, is what a bare save reports first.
    const Outcome nowhere = planner.dispatch(Save{std::nullopt});
    CHECK(!nowhere.ok);
    CHECK(nowhere.kind == Kind::Context);
    CHECK(nowhere.message.find("came from nowhere") != std::string::npos);
    CHECK(document.depth() == 0);  // and a save that failed is not a checkpoint

    const Outcome unsaved = planner.dispatch(Save{std::string{"out.svg"}});
    CHECK(!unsaved.ok);
    CHECK(unsaved.message.find("serializer is not implemented") != std::string::npos);

    // `init` replaces the document instead of editing it: the tree, the history, the
    // file behind it and the canvas all start over, and the canvas it names is what
    // the new root carries.
    const Outcome fresh = planner.dispatch(Init{Length{400.0F, Length::Unit::UserUnit}, std::nullopt});
    CHECK(fresh.ok);
    CHECK(fresh.kind == Kind::Context);
    CHECK(document.children(document.root()).empty());
    CHECK(document.find("box") == std::nullopt);
    CHECK(document.depth() == 0);
    CHECK(!document.can_undo());
    CHECK(document.source_path().empty());
    if (const std::optional<Node> payload = document.node(document.root())) {
        const Container& container = std::get<Container>(std::get<SVGNode>(*payload).data);
        const Container::SVG& canvas = std::get<Container::SVG>(container.specific);
        CHECK(canvas.width.has_value() && canvas.height.has_value());
        if (canvas.width) CHECK(to_text(*canvas.width) == "400");
        if (canvas.height) CHECK(to_text(*canvas.height) == "600");  // the side left out
    }

    if (failures != 0) {
        std::printf("pipeline: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("pipeline: all checks passed\n");
    return 0;
}
