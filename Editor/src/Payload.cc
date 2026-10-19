// The edits that change what a node holds and nothing about where it sits: a move and a
// scale, which may change a leaf's transform, a leaf's own values, or a container's
// descendants, and the canvas, which is the same arithmetic on the root element's values.
//
// Every edit is one transaction, which is what makes it one undo step: the nodes keep
// their places in the tree, so their handles still name them after.
#include <cmath>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Geometry.hh"
#include "Naming.hh"
#include "NodeRef.hh"
#include "Outcome.hh"
#include "Translations.hh"

namespace {

// The node kind decides what an omitted selector means. A container has no params of its
// own, so it reaches its descendants; a leaf has params and no descendants to reach.
std::expected<EditMode, Refusal> resolve_mode(const Node& payload, EditTarget target, EditScope scope) {
    const bool container = is_container(payload);
    if (target == EditTarget::Transform) {
        if (scope != EditScope::Default) return std::unexpected(Refusal{fault::TransformHasNoScope{}});
        if (container) return std::unexpected(Refusal{fault::TransformOnContainer{}});
        return EditMode::Transform;
    }

    if (scope == EditScope::Default) scope = container ? EditScope::Cascade : EditScope::Restrict;
    if (scope == EditScope::Cascade) {
        if (!container) return std::unexpected(Refusal{fault::CascadeRequiresContainer{}});
        return EditMode::Cascade;
    }
    // A container has no params of its own, so restricting the edit to the node means
    // changing the one value it does own: its transform.
    if (container) return EditMode::ContainerTransform;
    return EditMode::Params;
}

// Whether a node already has a coordinate system of its own. A parent-space delta written
// into the params under such a node would be read in that node's local units, which is
// exactly how a 2x transform turns a move of 1 into 2.
bool carries_transform(const Node& payload) {
    return std::holds_alternative<SVGNode>(payload) &&
           std::get<SVGNode>(payload).transform_attr.head != no_id;
}

// A cascade changes params where it safely can, and stops at every node that already has
// a transform. At that boundary the operation is prepended to the node's transform
// instead: the whole subtree then moves or scales in the parent's coordinate system, and
// the values below keep the units and scale they were written with.
struct CascadePlan {
    std::vector<std::pair<Id, Node>> params;
    std::vector<Id> transformed;
};

// The plan is collected before a transaction opens, so one bad param refuses the whole
// cascade without leaving a partial edit behind.
template<typename Apply>
std::optional<Refusal> plan_descendants(const Document& document, Id parent, Apply apply,
                                        CascadePlan& plan) {
    for (const Id child : document.children(parent)) {
        std::optional<Node> payload = document.node(child);
        if (!payload || std::holds_alternative<PlainText>(*payload)) continue;

        // Prepending here applies to this node and everything under it, so descending
        // would apply the operation a second time in the wrong coordinate system.
        if (carries_transform(*payload)) {
            plan.transformed.push_back(child);
            continue;
        }

        if (!is_container(*payload)) {
            if (std::optional<Refusal> problem = apply(*payload)) return problem;
            plan.params.emplace_back(child, std::move(*payload));
        }
        if (std::optional<Refusal> problem = plan_descendants(document, child, apply, plan)) {
            return problem;
        }
    }
    return std::nullopt;
}

void commit_cascade(Document& document, CascadePlan& plan, const Transform& operation) {
    if (plan.params.empty() && plan.transformed.empty()) return;
    Transaction edit = document.transaction();
    for (auto& [id, payload] : plan.params) edit.update(id, std::move(payload));
    for (const Id node : plan.transformed) edit.prepend_transform(node, operation);
    edit.commit();
}

}  // namespace

template<> Outcome Editor::handle<Move>(const Move& intent) {
    const std::expected<Id, Refusal> node = resolve(intent.node);
    if (!node) return refused(Kind::Change, node.error());

    std::optional<Node> payload = document_.node(*node);
    if (!payload) return refused(Kind::Change, fault::InternalError{});

    const std::expected<EditMode, Refusal> mode = resolve_mode(*payload, intent.target, intent.scope);
    if (!mode) return refused(Kind::Change, mode.error());

    const bool zero_offset = std::holds_alternative<Length>(intent.dx) &&
                             std::holds_alternative<Length>(intent.dy) &&
                             std::get<Length>(intent.dx).value == 0.0F &&
                             std::get<Length>(intent.dy).value == 0.0F;
    if (zero_offset) return happened(Kind::Change, event::NothingMoved{});

    const event::NodeName named = name_of(document_, *node);
    const fault::EmptyContainer empty{named.element, named.svg_id};

    if (*mode == EditMode::ContainerTransform && document_.children(*node).empty()) {
        return refused(Kind::Change, empty);
    }

    if (*mode == EditMode::Transform || *mode == EditMode::ContainerTransform) {
        if (!std::holds_alternative<SVGNode>(*payload)) {
            return refused(Kind::Change, fault::TextCannotMove{});
        }
        if (!is_finite(intent.dx)) return refused(Kind::Change, fault::NonFiniteValue{"offset x"});
        if (!is_finite(intent.dy)) return refused(Kind::Change, fault::NonFiniteValue{"offset y"});

        Transaction edit = document_.transaction();
        edit.prepend_transform(*node, Translate{intent.dx, intent.dy});
        edit.commit();
        return happened(Kind::Change, event::Moved{named, intent.dx, intent.dy, *mode, 1});
    }

    if (*mode == EditMode::Params) {
        std::optional<Refusal> problem = Geometry::translate_params(*payload, intent.dx, intent.dy);
        if (problem) return refused(Kind::Change, std::move(*problem));

        Transaction edit = document_.transaction();
        edit.update(*node, std::move(*payload));
        edit.commit();
        return happened(Kind::Change, event::Moved{named, intent.dx, intent.dy, EditMode::Params, 1});
    }

    CascadePlan plan;
    std::optional<Refusal> problem = plan_descendants(
        document_, *node,
        [&intent](Node& child) { return Geometry::translate_params(child, intent.dx, intent.dy); }, plan);
    if (problem) return refused(Kind::Change, std::move(*problem));
    if (plan.params.empty() && plan.transformed.empty()) {
        return refused(Kind::Change, empty);
    }
    commit_cascade(document_, plan, Translate{intent.dx, intent.dy});
    return happened(Kind::Change,
                    event::Moved{named, intent.dx, intent.dy, EditMode::Cascade,
                                 plan.params.size() + plan.transformed.size()});
}

template<> Outcome Editor::handle<ScaleBy>(const ScaleBy& intent) {
    const std::expected<Id, Refusal> node = resolve(intent.node);
    if (!node) return refused(Kind::Change, node.error());

    if (!std::isfinite(intent.factor) || !(intent.factor > 0.0F)) {
        return refused(Kind::Change, fault::ScaleFactorNotPositive{});
    }

    std::optional<Node> payload = document_.node(*node);
    if (!payload) return refused(Kind::Change, fault::InternalError{});

    const std::expected<EditMode, Refusal> mode = resolve_mode(*payload, intent.target, intent.scope);
    if (!mode) return refused(Kind::Change, mode.error());

    const event::NodeName named = name_of(document_, *node);
    const fault::EmptyContainer empty{named.element, named.svg_id};

    if (*mode == EditMode::ContainerTransform && document_.children(*node).empty()) {
        return refused(Kind::Change, empty);
    }

    if (*mode == EditMode::Transform || *mode == EditMode::ContainerTransform) {
        if (!std::holds_alternative<SVGNode>(*payload)) {
            return refused(Kind::Change, fault::TextCannotScale{});
        }

        Transaction edit = document_.transaction();
        edit.prepend_transform(*node, Scale{intent.factor, intent.factor});
        edit.commit();
        return happened(Kind::Change, event::Scaled{named, intent.factor, *mode, 1});
    }

    if (*mode == EditMode::Params) {
        std::optional<Refusal> problem = Geometry::scale_params(*payload, intent.factor);
        if (problem) return refused(Kind::Change, std::move(*problem));

        Transaction edit = document_.transaction();
        edit.update(*node, std::move(*payload));
        edit.commit();
        return happened(Kind::Change, event::Scaled{named, intent.factor, EditMode::Params, 1});
    }

    CascadePlan plan;
    std::optional<Refusal> problem = plan_descendants(
        document_, *node, [&intent](Node& child) { return Geometry::scale_params(child, intent.factor); }, plan);
    if (problem) return refused(Kind::Change, std::move(*problem));
    if (plan.params.empty() && plan.transformed.empty()) {
        return refused(Kind::Change, empty);
    }
    commit_cascade(document_, plan, Scale{intent.factor, intent.factor});
    return happened(Kind::Change,
                    event::Scaled{named, intent.factor, EditMode::Cascade,
                                  plan.params.size() + plan.transformed.size()});
}

template<> Outcome Editor::handle<SetCanvas>(const SetCanvas& intent) {
    if (intent.width && !is_finite(*intent.width)) {
        return refused(Kind::Change, fault::NonFiniteValue{"width"});
    }
    if (intent.height && !is_finite(*intent.height)) {
        return refused(Kind::Change, fault::NonFiniteValue{"height"});
    }
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
    // the canvas before it goes with the history that remembered it. Like `init`, `read`
    // and `save`, this is a point outside the editable history rather than an edit in it.
    document_.clear_history(true);
    return happened(Kind::Change, event::CanvasSet{size.width, size.height});
}
