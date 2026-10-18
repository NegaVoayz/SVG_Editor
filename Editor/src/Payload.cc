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
    const std::expected<Id, Refusal> node = resolve(intent.node);
    if (!node) return refused(Kind::Change, node.error());

    const bool zero_offset = std::holds_alternative<Length>(intent.dx) &&
                             std::holds_alternative<Length>(intent.dy) &&
                             std::get<Length>(intent.dx).value == 0.0F &&
                             std::get<Length>(intent.dy).value == 0.0F;
    if (zero_offset) return happened(Kind::Change, event::NothingMoved{});

    std::optional<Node> payload = document_.node(*node);
    if (!payload) return refused(Kind::Change, fault::InternalError{});

    std::optional<Refusal> problem = Geometry::translate(*payload, intent.dx, intent.dy);
    if (problem) return refused(Kind::Change, std::move(*problem));

    Transaction edit = document_.transaction();
    edit.update(*node, std::move(*payload));
    edit.commit();
    return happened(Kind::Change, event::Moved{*node, intent.dx, intent.dy});
}

template<> Outcome Editor::handle<ScaleBy>(const ScaleBy& intent) {
    const std::expected<Id, Refusal> node = resolve(intent.node);
    if (!node) return refused(Kind::Change, node.error());

    std::optional<Node> payload = document_.node(*node);
    if (!payload) return refused(Kind::Change, fault::InternalError{});

    std::optional<Refusal> problem = Geometry::scale(*payload, intent.factor);
    if (problem) return refused(Kind::Change, std::move(*problem));

    Transaction edit = document_.transaction();
    edit.update(*node, std::move(*payload));
    edit.commit();
    return happened(Kind::Change, event::Scaled{*node, intent.factor});
}

template<> Outcome Editor::handle<SetCanvas>(const SetCanvas& intent) {
    if (intent.width && numeric_value(*intent.width) <= 0.0F) {
        return refused(Kind::Change, fault::NonPositiveCanvas{"width"});
    }
    if (intent.height && numeric_value(*intent.height) <= 0.0F) {
        return refused(Kind::Change, fault::NonPositiveCanvas{"height"});
    }

    const Id root = document_.root();
    std::optional<Node> payload = document_.node(root);
    if (!payload || !std::holds_alternative<SVGNode>(*payload)) {
        return refused(Kind::Change, fault::NoRootElement{});
    }

    SVGNode& element = std::get<SVGNode>(*payload);
    if (!std::holds_alternative<Container>(element.data)) {
        return refused(Kind::Change, fault::RootIsNotContainer{});
    }
    Container& container = std::get<Container>(element.data);
    if (!std::holds_alternative<Container::SVG>(container.specific)) {
        return refused(Kind::Change, fault::RootIsNotSvg{});
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
    return happened(Kind::Change, event::CanvasSet{size.width, size.height});
}
