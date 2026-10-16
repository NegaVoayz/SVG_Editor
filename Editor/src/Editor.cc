#include "Editor.hh"

#include <cstddef>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include "Geometry.hh"

namespace {

// The two elements the Editor can build without a parser: a group, and a rect.
Node make_group(std::string svg_id) {
    SVGNode element;
    element.identity_attr = IdentityAttr{std::move(svg_id), "", no_id};
    element.data = Container{ChildAttr{no_id}, Container::Group{}};
    return Node{std::move(element)};
}

Node make_rect(std::string svg_id, Shape::Rect geometry, std::optional<Color> fill) {
    SVGNode element;
    element.identity_attr = IdentityAttr{std::move(svg_id), "", no_id};
    if (fill) element.paint_attr.fill = Paint{*fill};
    element.data = Shape{std::move(geometry)};
    return Node{std::move(element)};
}

Outcome failed(std::string message) { return Outcome{false, Kind::Change, std::move(message)}; }

}  // namespace

Editor::Editor(Document& document) : document_(document) {}

// The translations, one explicit specialization per intent. They have to be
// declared before translate() uses them: a specialization that the compiler has not
// seen yet would leave the primary template - the build error - in place.
template<> Outcome Editor::handle<CreateRect>(const CreateRect& intent) { return create(intent); }
template<> Outcome Editor::handle<CreateGroup>(const CreateGroup& intent) { return create(intent); }
template<> Outcome Editor::handle<Delete>(const Delete& intent) { return remove(intent); }
template<> Outcome Editor::handle<Move>(const Move& intent) { return move(intent); }
template<> Outcome Editor::handle<AddTo>(const AddTo& intent) { return addto(intent); }
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

Id Editor::look_up(const RootRef&) const { return document_.root(); }

Id Editor::look_up(const HandleRef& reference) const {
    if (!document_.node(reference.id)) {
        throw std::invalid_argument("no node lives at handle #" + std::to_string(reference.id));
    }
    return reference.id;
}

Id Editor::look_up(const SvgIdRef& reference) const {
    const std::optional<Id> found = document_.find(reference.name);
    if (!found) throw std::invalid_argument("no node is called \"" + reference.name + "\"");
    return *found;
}

Id Editor::resolve(const NodeRef& reference) const {
    return std::visit([this](const auto& which) { return look_up(which); }, reference);
}

Outcome Editor::create(const CreateGroup& intent) {
    if (document_.find(intent.id)) return failed("an element is already called \"" + intent.id + "\"");

    // An element starts out last under the root: addto moves it elsewhere, and a
    // node that belonged to nothing would be unreachable and unreclaimable.
    Transaction edit = document_.transaction();
    const Id node = edit.append_child(document_.root(), make_group(intent.id));
    edit.commit();
    return Outcome{true, Kind::Change,
                   "created #" + std::to_string(node) + " <g> id=\"" + intent.id + "\" under the root"};
}

Outcome Editor::create(const CreateRect& intent) {
    if (intent.id == "/") return failed("'/' is the root, which already exists");
    if (document_.find(intent.id)) {
        return failed("an element is already called \"" + intent.id + "\"");
    }
    Transaction edit = document_.transaction();
    const Id node = edit.append_child(document_.root(),
                                      make_rect(intent.id,
                                                Shape::Rect{intent.x, intent.y, intent.width, intent.height},
                                                intent.fill));
    edit.commit();
    return Outcome{true, Kind::Change,
                   "created #" + std::to_string(node) + " <rect> id=\"" + intent.id + "\" under the root"};
}

Outcome Editor::remove(const Delete& intent) {
    const Id node = resolve(intent.node);
    Transaction edit = document_.transaction();
    edit.erase(node);
    edit.commit();
    return Outcome{true, Kind::Change, "deleted #" + std::to_string(node) + " and everything under it"};
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

Outcome Editor::addto(const AddTo& intent) {
    const Id parent = resolve(intent.parent);
    const Id node = resolve(intent.node);

    Transaction edit = document_.transaction();
    edit.attach_last(parent, node);
    edit.commit();
    return Outcome{true, Kind::Change,
                   "moved #" + std::to_string(node) + " to the end of #" + std::to_string(parent)};
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
