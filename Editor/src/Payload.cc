// The edits that change what a node holds and nothing about where it sits: a move and a
// scale, which are arithmetic on the node's own values, and the canvas, which is the
// arithmetic's other end - the values the root element holds.
//
// All three are one payload patch and one transaction, which is what makes an edit one
// handle wide: the node keeps its place in the tree, so the handle still names it after.
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "Geometry.hh"
#include "NodeRef.hh"
#include "Outcome.hh"
#include "Translations.hh"

template<> Outcome Editor::handle<Move>(const Move& intent) {
    const Id node = resolve(intent.node);

    const bool zero_offset = std::holds_alternative<Length>(intent.dx) &&
                             std::holds_alternative<Length>(intent.dy) &&
                             std::get<Length>(intent.dx).value == 0.0F &&
                             std::get<Length>(intent.dy).value == 0.0F;
    if (zero_offset) return Outcome{true, Kind::Change, "the offset is zero, nothing moved"};

    std::optional<Node> payload = document_.node(node);
    if (!payload) return refusal(Kind::Change, "no node lives at that handle");

    std::string reason;
    if (!Geometry::translate(*payload, intent.dx, intent.dy, reason)) return refusal(Kind::Change, reason);

    Transaction edit = document_.transaction();
    edit.update(node, std::move(*payload));
    edit.commit();
    return Outcome{true, Kind::Change, "moved #" + std::to_string(node) + " by " + to_text(intent.dx) + " " +
                                           to_text(intent.dy)};
}

template<> Outcome Editor::handle<ScaleBy>(const ScaleBy& intent) {
    const Id node = resolve(intent.node);

    std::optional<Node> payload = document_.node(node);
    if (!payload) return refusal(Kind::Change, "no node lives at that handle");

    std::string reason;
    if (!Geometry::scale(*payload, intent.factor, reason)) return refusal(Kind::Change, reason);

    Transaction edit = document_.transaction();
    edit.update(node, std::move(*payload));
    edit.commit();
    return Outcome{true, Kind::Change,
                   "scaled #" + std::to_string(node) + " by " + number_text(intent.factor) +
                       ", so everything drawn with it grew by that much"};
}

template<> Outcome Editor::handle<SetCanvas>(const SetCanvas& intent) {
    const Id root = document_.root();
    std::optional<Node> payload = document_.node(root);
    if (!payload || !std::holds_alternative<SVGNode>(*payload)) {
        return refusal(Kind::Change, "the document has no root element");
    }

    SVGNode& element = std::get<SVGNode>(*payload);
    if (!std::holds_alternative<Container>(element.data)) {
        return refusal(Kind::Change, "the root is not a container");
    }
    Container& container = std::get<Container>(element.data);
    if (!std::holds_alternative<Container::SVG>(container.specific)) {
        return refusal(Kind::Change, "the root is not an <svg> element, so it has no canvas");
    }
    Container::SVG& size = std::get<Container::SVG>(container.specific);
    if (intent.width) size.width = intent.width;
    if (intent.height) size.height = intent.height;

    Transaction edit = document_.transaction();
    edit.update(root, std::move(*payload));
    edit.commit();
    // The canvas is what the frame behind the drawing is from here on, so the way back to
    // the canvas before it goes with the history that remembered it.
    document_.clear_history();
    return Outcome{true, Kind::Change, "the canvas is now " + to_text(*size.width) + " x " + to_text(*size.height) +
                                           ", and the history starts from here"};
}
