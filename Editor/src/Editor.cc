#include "Editor.hh"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "Geometry.hh"

namespace {

// One element with an id and a body: every create builds its own body and comes here
// for the two fields they all share.
Node element(std::string svg_id, std::variant<Shape, Container, Text> body) {
    SVGNode node;
    node.identity_attr = IdentityAttr{std::move(svg_id), "", no_id};
    node.data = std::move(body);
    return Node{std::move(node)};
}

// The character data inside a text element. It is a node of its own, which is why a
// text element is written in two steps and why its words stay where SVG keeps them -
// inside the element, rather than in a string field of it.
Node characters(std::string content) { return Node{PlainText{std::move(content)}}; }

void fill(Node& payload, const std::optional<Color>& color) {
    if (!color) return;
    std::get<SVGNode>(payload).paint_attr.fill = Paint{*color};
}

// A line has no inside to fill, so the one colour it takes is its stroke.
void stroke(Node& payload, const std::optional<Color>& color) {
    if (!color) return;
    std::get<SVGNode>(payload).paint_attr.stroke = Paint{*color};
}

Outcome failed(std::string message) { return Outcome{false, Kind::Change, std::move(message)}; }

}  // namespace

Editor::Editor(Document& document) : document_(document) {}

// The translations, one explicit specialization per intent. They have to be
// declared before translate() uses them: a specialization the compiler has not seen
// yet would leave the primary template - the build error - in place.
template<> Outcome Editor::handle<CreateRect>(const CreateRect& intent) { return create(intent); }
template<> Outcome Editor::handle<CreateCircle>(const CreateCircle& intent) { return create(intent); }
template<> Outcome Editor::handle<CreateEllipse>(const CreateEllipse& intent) { return create(intent); }
template<> Outcome Editor::handle<CreateLine>(const CreateLine& intent) { return create(intent); }
template<> Outcome Editor::handle<CreateText>(const CreateText& intent) { return create(intent); }
template<> Outcome Editor::handle<CreateGroup>(const CreateGroup& intent) { return create(intent); }
template<> Outcome Editor::handle<Delete>(const Delete& intent) { return remove(intent); }
template<> Outcome Editor::handle<Move>(const Move& intent) { return move(intent); }
template<> Outcome Editor::handle<ScaleBy>(const ScaleBy& intent) { return scale(intent); }
template<> Outcome Editor::handle<Group>(const Group& intent) { return group(intent); }
template<> Outcome Editor::handle<Ungroup>(const Ungroup& intent) { return ungroup(intent); }
template<> Outcome Editor::handle<AddTo>(const AddTo& intent) { return addto(intent); }
template<> Outcome Editor::handle<Front>(const Front& intent) { return front(intent); }
template<> Outcome Editor::handle<Back>(const Back& intent) { return back(intent); }
template<> Outcome Editor::handle<SetCanvas>(const SetCanvas& intent) { return canvas(intent); }
template<> Outcome Editor::handle<Undo>(const Undo&) { return undo(); }
template<> Outcome Editor::handle<Redo>(const Redo&) { return redo(); }

Outcome Editor::translate(const ChangeIntent& intent) {
    try {
        return std::visit([this](const auto& value) { return handle(value); }, intent);
    } catch (const std::exception& error) {
        // Whatever refused - a handle that names nothing, a tree that would become
        // illegal - the transaction that was open is gone and the Model rolled it
        // back. All that is left is the sentence for the user.
        return failed("the edit was not applied: " + std::string(error.what()));
    }
}

Id Editor::resolve(const NodeRef& reference) const {
    const std::optional<Id> found = look_up(document_, reference);
    if (found) return *found;
    if (std::holds_alternative<HandleRef>(reference)) {
        throw std::invalid_argument("no node lives at handle #" + std::to_string(std::get<HandleRef>(reference).id));
    }
    throw std::invalid_argument("no node is called " + describe(reference));
}

Outcome Editor::add(const std::string& id, std::string_view kind, const NodeRef& parent_ref, Node payload) {
    if (document_.find(id)) return failed("an element is already called \"" + id + "\"");
    const std::optional<Id> parent = look_up(document_, parent_ref);
    if (!parent) return failed("there is nothing to put the new element in: " + describe(parent_ref));

    Transaction edit = document_.transaction();
    const Id node = edit.append_child(*parent, std::move(payload));
    edit.commit();
    return Outcome{true, Kind::Change, "created #" + std::to_string(node) + " <" + std::string{kind} + "> id=\"" + id +
                                           "\" in " + describe(parent_ref)};
}

Outcome Editor::create(const CreateGroup& intent) {
    return add(intent.id, "g", intent.parent, element(intent.id, Container{ChildAttr{no_id}, Container::Group{}}));
}

Outcome Editor::create(const CreateRect& intent) {
    Node payload = element(intent.id, Shape{Shape::Rect{intent.x, intent.y, intent.width, intent.height}});
    fill(payload, intent.fill);
    return add(intent.id, "rect", intent.parent, std::move(payload));
}

Outcome Editor::create(const CreateCircle& intent) {
    Node payload = element(intent.id, Shape{Shape::Circle{intent.cx, intent.cy, intent.r}});
    fill(payload, intent.fill);
    return add(intent.id, "circle", intent.parent, std::move(payload));
}

Outcome Editor::create(const CreateEllipse& intent) {
    Node payload = element(intent.id, Shape{Shape::Ellipse{intent.cx, intent.cy, intent.rx, intent.ry}});
    fill(payload, intent.fill);
    return add(intent.id, "ellipse", intent.parent, std::move(payload));
}

Outcome Editor::create(const CreateLine& intent) {
    Node payload = element(intent.id, Shape{Shape::Line{intent.x1, intent.y1, intent.x2, intent.y2}});
    stroke(payload, intent.stroke);
    return add(intent.id, "line", intent.parent, std::move(payload));
}

Outcome Editor::create(const CreateText& intent) {
    if (document_.find(intent.id)) return failed("an element is already called \"" + intent.id + "\"");
    const std::optional<Id> parent = look_up(document_, intent.parent);
    if (!parent) return failed("there is nothing to put the new element in: " + describe(intent.parent));

    Text text;
    text.common.x = intent.x;
    text.common.y = intent.y;
    text.child_attr = ChildAttr{no_id};

    // The element and its content are two nodes, and one instruction is one
    // transaction, so either both are written or neither is.
    Transaction edit = document_.transaction();
    const Id node = edit.append_child(*parent, element(intent.id, std::move(text)));
    edit.append_child(node, characters(intent.content));
    edit.commit();
    return Outcome{true, Kind::Change, "created #" + std::to_string(node) + " <text> id=\"" + intent.id + "\" in " +
                                           describe(intent.parent) + ", holding \"" + intent.content + "\""};
}

Outcome Editor::remove(const Delete& intent) {
    const Id node = resolve(intent.node);

    Transaction edit = document_.transaction();
    edit.erase(node);
    edit.commit();
    return Outcome{true, Kind::Change, "deleted " + describe(intent.node) + " (#" + std::to_string(node) +
                                           ") and everything under it"};
}

Outcome Editor::move(const Move& intent) {
    const Id node = resolve(intent.node);

    const bool zero_offset = std::holds_alternative<Length>(intent.dx) &&
                             std::holds_alternative<Length>(intent.dy) &&
                             std::get<Length>(intent.dx).value == 0.0F &&
                             std::get<Length>(intent.dy).value == 0.0F;
    if (zero_offset) return Outcome{true, Kind::Change, "the offset is zero, nothing moved"};

    // A translation is a payload edit: the node keeps its place in the tree, only
    // its geometry changes, so one handle still names the same element after it.
    std::optional<Node> payload = document_.node(node);
    if (!payload) return failed("no node lives at that handle");

    std::string reason;
    if (!Geometry::translate(*payload, intent.dx, intent.dy, reason)) return failed(reason);

    Transaction edit = document_.transaction();
    edit.update(node, std::move(*payload));
    edit.commit();
    return Outcome{true, Kind::Change, "moved #" + std::to_string(node) + " by " + to_text(intent.dx) + " " +
                                           to_text(intent.dy)};
}

Outcome Editor::scale(const ScaleBy& intent) {
    const Id node = resolve(intent.node);

    std::optional<Node> payload = document_.node(node);
    if (!payload) return failed("no node lives at that handle");

    std::string reason;
    if (!Geometry::scale(*payload, intent.factor, reason)) return failed(reason);

    Transaction edit = document_.transaction();
    edit.update(node, std::move(*payload));
    edit.commit();
    return Outcome{true, Kind::Change,
                   "scaled #" + std::to_string(node) + " by " + number_text(intent.factor) +
                       ", so everything drawn with it grew by that much"};
}

Outcome Editor::group(const Group& intent) {
    if (intent.nodes.empty()) return failed("a group needs at least one node to hold");
    if (document_.find(intent.id)) return failed("an element is already called \"" + intent.id + "\"");

    std::vector<Id> members;
    members.reserve(intent.nodes.size());
    for (const NodeRef& reference : intent.nodes) members.push_back(resolve(reference));

    // The group takes the place of the first node it takes, which means standing in
    // front of the first sibling after that node which is not being taken itself.
    const Id first = members.front();
    const std::optional<Id> parent = document_.parent(first);
    if (!parent) return failed("the root cannot be put inside a group");

    const auto taken = [&members](Id node) {
        return std::find(members.begin(), members.end(), node) != members.end();
    };
    Id anchor = no_id;  // nothing stands after them: the end of the list
    bool past_first = false;
    for (const Id sibling : document_.children(*parent)) {
        if (sibling == first) {
            past_first = true;
            continue;
        }
        if (past_first && !taken(sibling)) {
            anchor = sibling;
            break;
        }
    }

    Transaction edit = document_.transaction();
    const Id made =
        edit.insert_at(*parent, anchor, element(intent.id, Container{ChildAttr{no_id}, Container::Group{}}));
    // In the order they were written, so the group reads the way the command did.
    for (const Id member : members) edit.attach_last(made, member);
    edit.commit();
    return Outcome{true, Kind::Change, "created #" + std::to_string(made) + " <g> id=\"" + intent.id + "\" holding " +
                                           std::to_string(members.size()) + " node(s)"};
}

Outcome Editor::ungroup(const Ungroup& intent) {
    const Id node = resolve(intent.node);
    if (node == document_.root()) return failed("the root is the document itself and cannot be ungrouped");

    const std::optional<Node> payload = document_.node(node);
    if (!payload) return failed("no node lives at that handle");
    if (!std::holds_alternative<SVGNode>(*payload) ||
        !std::holds_alternative<Container>(std::get<SVGNode>(*payload).data)) {
        return failed("only a container can be ungrouped: a shape has nothing inside it, and a text element holds "
                      "its words rather than children that could stand where it stands");
    }

    const std::optional<Id> parent = document_.parent(node);
    if (!parent) return failed("the root is the document itself and cannot be ungrouped");

    const std::vector<Id> members = document_.children(node);
    // What stood after the group keeps standing after its children: each of them is
    // placed in front of that node, in order, which leaves them in the order they had.
    const std::vector<Id> siblings = document_.children(*parent);
    const auto at = std::find(siblings.begin(), siblings.end(), node);
    const Id anchor = (at != siblings.end() && at + 1 != siblings.end()) ? *(at + 1) : no_id;

    Transaction edit = document_.transaction();
    for (const Id member : members) edit.attach_at(*parent, anchor, member);
    edit.erase(node);
    edit.commit();
    return Outcome{true, Kind::Change, "released " + std::to_string(members.size()) + " node(s) into #" +
                                           std::to_string(*parent) + " and erased " + describe(intent.node)};
}

Outcome Editor::addto(const AddTo& intent) {
    const Id parent = resolve(intent.parent);
    const Id node = resolve(intent.node);

    Transaction edit = document_.transaction();
    edit.attach_last(parent, node);
    edit.commit();
    return Outcome{true, Kind::Change,
                   "moved #" + std::to_string(node) + " to the end of #" + std::to_string(parent)};
}

Outcome Editor::front(const Front& intent) {
    const Id node = resolve(intent.node);
    const std::optional<Id> parent = document_.parent(node);
    if (!parent) return failed("the root is the whole document: it has no siblings to stand in front of");

    const std::vector<Id> siblings = document_.children(*parent);
    if (!siblings.empty() && siblings.front() == node) {
        return Outcome{true, Kind::Change, describe(intent.node) + " is already the first child"};
    }

    Transaction edit = document_.transaction();
    edit.attach_at(*parent, siblings.front(), node);
    edit.commit();
    return Outcome{true, Kind::Change,
                   "moved #" + std::to_string(node) + " to the front of #" + std::to_string(*parent)};
}

Outcome Editor::back(const Back& intent) {
    const Id node = resolve(intent.node);
    const std::optional<Id> parent = document_.parent(node);
    if (!parent) return failed("the root is the whole document: it has no siblings to stand behind");

    const std::vector<Id> siblings = document_.children(*parent);
    if (!siblings.empty() && siblings.back() == node) {
        return Outcome{true, Kind::Change, describe(intent.node) + " is already the last child"};
    }

    Transaction edit = document_.transaction();
    edit.attach_last(*parent, node);
    edit.commit();
    return Outcome{true, Kind::Change,
                   "moved #" + std::to_string(node) + " to the back of #" + std::to_string(*parent)};
}

Outcome Editor::canvas(const SetCanvas& intent) {
    const Id root = document_.root();
    std::optional<Node> payload = document_.node(root);
    if (!payload || !std::holds_alternative<SVGNode>(*payload)) return failed("the document has no root element");

    SVGNode& element = std::get<SVGNode>(*payload);
    if (!std::holds_alternative<Container>(element.data)) return failed("the root is not a container");
    Container& container = std::get<Container>(element.data);
    if (!std::holds_alternative<Container::SVG>(container.specific)) {
        return failed("the root is not an <svg> element, so it has no canvas");
    }
    Container::SVG& size = std::get<Container::SVG>(container.specific);
    if (intent.width) size.width = intent.width;
    if (intent.height) size.height = intent.height;

    Transaction edit = document_.transaction();
    edit.update(root, std::move(*payload));
    edit.commit();
    // The canvas is what the frame behind the drawing is from here on, so the way
    // back to the canvas before it goes with the history that remembered it.
    document_.clear_history();
    return Outcome{true, Kind::Change, "the canvas is now " + to_text(*size.width) + " x " + to_text(*size.height) +
                                           ", and the history starts from here"};
}

Outcome Editor::undo() {
    if (!document_.can_undo()) return failed("there is nothing left to undo");

    document_.undo();
    return Outcome{true, Kind::Change, "undone, at " + std::to_string(document_.position()) + " of " +
                                           std::to_string(document_.depth()) + " edits"};
}

Outcome Editor::redo() {
    if (!document_.can_redo()) return failed("there is nothing left to redo");

    document_.redo();
    return Outcome{true, Kind::Change, "redone, at " + std::to_string(document_.position()) + " of " +
                                           std::to_string(document_.depth()) + " edits"};
}
