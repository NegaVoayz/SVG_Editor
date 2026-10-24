// The edits that change what a node holds and nothing about where it sits: a move and a
// scale, which may change a node's transform, a leaf's own values, or a container's
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
#include "Units.hh"

namespace {

// Whether a node already has a coordinate system of its own. A parent-space value written
// into the params under such a node would be read in that node's local units, which is
// exactly how a 90-degree rotation turns an x offset into a y offset.
bool carries_transform(const Node& payload) {
    return std::holds_alternative<SVGNode>(payload) &&
           !is_identity(std::get<SVGNode>(payload).transform_attr.transform);
}

// Omitted selectors mean params and cascade. A container's cascade walks its descendants;
// a non-container has no container descendants, so its cascade changes the node itself.
//
// Params are read in the coordinate system the node's own transform establishes, while a
// move or scale offset is in the parent's. Once the node carries a transform, those are
// different systems, so the operation pre-multiplies the matrix: `T(10 0) * R(90)` moves
// right, where writing x += 10 under the rotation moves down. A matrix edit is exact for
// every form SVG allows, including a skew, a non-uniform scale or a unit-bearing
// translation, where converting the offset into local params would have to approximate the
// inverse. This is the same boundary a cascade already applies to a transformed
// descendant; here it is the node the instruction named.
std::expected<EditMode, Refusal> resolve_mode(const Node& payload, EditTarget target, EditScope scope) {
    const bool container = is_container(payload);
    const bool transformed = carries_transform(payload);
    if (target == EditTarget::Transform) {
        if (scope != EditScope::Default) return std::unexpected(Refusal{fault::TransformHasNoScope{}});
        return EditMode::Transform;
    }

    if (scope == EditScope::Default) scope = EditScope::Cascade;
    if (scope == EditScope::Cascade) {
        if (!container) return transformed ? EditMode::Transform : EditMode::Params;
        return transformed ? EditMode::Transform : EditMode::Cascade;
    }
    // A container has no params of its own, so restricting the edit to the node means
    // changing the one value it does own: its transform.
    if (container) return EditMode::ContainerTransform;
    return transformed ? EditMode::Transform : EditMode::Params;
}

// A cascade changes params where it safely can, and stops at every node that already has
// a transform. At that boundary the operation is pre-multiplied into the node's matrix
// instead: the whole subtree then moves or scales in the parent's coordinate system, and
// the values below keep the units and scale they were written with.
struct CascadePlan {
    std::vector<std::pair<Id, Node>> params;
    std::vector<std::pair<Id, Node>> transformed;
};

// The plan is collected before a transaction opens, so one bad param refuses the whole
// cascade without leaving a partial edit behind.
template<typename Apply>
std::optional<Refusal> plan_descendants(const Document& document, Id parent, const Transform& operation, Apply apply,
                                        CascadePlan& plan) {
    for (const Id child : document.children(parent)) {
        std::optional<Node> payload = document.node(child);
        if (!payload || std::holds_alternative<PlainText>(*payload)) continue;

        // Pre-multiplying here applies to this node and everything under it, so descending
        // would apply the operation a second time in the wrong coordinate system.
        if (carries_transform(*payload)) {
            const Transform& current = std::get<SVGNode>(*payload).transform_attr.transform;
            if (!Geometry::sum(operation.e, current.e)) {
                return Refusal{fault::IncompatibleLengths{operation.e, current.e}};
            }
            if (!Geometry::sum(operation.f, current.f)) {
                return Refusal{fault::IncompatibleLengths{operation.f, current.f}};
            }
            const std::optional<Transform> composed = compose(operation, current);
            if (!composed) return Refusal{fault::IncompatibleLengths{operation.e, current.e}};
            std::get<SVGNode>(*payload).transform_attr.transform = *composed;
            plan.transformed.emplace_back(child, std::move(*payload));
            continue;
        }

        if (!is_container(*payload)) {
            if (std::optional<Refusal> problem = apply(*payload)) return problem;
            plan.params.emplace_back(child, std::move(*payload));
        }
        if (std::optional<Refusal> problem = plan_descendants(document, child, operation, apply, plan)) {
            return problem;
        }
    }
    return std::nullopt;
}

void commit_cascade(Document& document, CascadePlan& plan) {
    if (plan.params.empty() && plan.transformed.empty()) return;
    Transaction edit = document.transaction();
    for (auto& [id, payload] : plan.params) edit.update(id, std::move(payload));
    for (auto& [id, payload] : plan.transformed) edit.update(id, std::move(payload));
    commit_edit(edit);
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

    if ((*mode == EditMode::Transform || *mode == EditMode::ContainerTransform) && is_container(*payload) &&
        document_.children(*node).empty()) {
        return refused(Kind::Change, empty);
    }

    if (*mode == EditMode::Transform || *mode == EditMode::ContainerTransform) {
        if (!std::holds_alternative<SVGNode>(*payload)) {
            return refused(Kind::Change, fault::TextCannotMove{});
        }
        if (!is_finite(intent.dx)) return refused(Kind::Change, fault::NonFiniteValue{"offset x"});
        if (!is_finite(intent.dy)) return refused(Kind::Change, fault::NonFiniteValue{"offset y"});

        const Transform& current = std::get<SVGNode>(*payload).transform_attr.transform;
        if (!Geometry::sum(intent.dx, current.e)) {
            return refused(Kind::Change, fault::IncompatibleLengths{intent.dx, current.e});
        }
        if (!Geometry::sum(intent.dy, current.f)) {
            return refused(Kind::Change, fault::IncompatibleLengths{intent.dy, current.f});
        }
        const std::optional<Transform> moved = compose(translated(intent.dx, intent.dy), current);
        if (!moved) return refused(Kind::Change, fault::IncompatibleLengths{intent.dx, current.e});
        std::get<SVGNode>(*payload).transform_attr.transform = *moved;

        Transaction edit = document_.transaction();
        edit.update(*node, std::move(*payload));
        commit_edit(edit);
        return happened(Kind::Change, event::Moved{named, intent.dx, intent.dy, *mode, 1});
    }

    if (*mode == EditMode::Params) {
        std::optional<Refusal> problem = Geometry::translate_params(*payload, intent.dx, intent.dy);
        if (problem) return refused(Kind::Change, std::move(*problem));

        Transaction edit = document_.transaction();
        edit.update(*node, std::move(*payload));
        commit_edit(edit);
        return happened(Kind::Change, event::Moved{named, intent.dx, intent.dy, EditMode::Params, 1});
    }

    const Transform operation = translated(intent.dx, intent.dy);
    CascadePlan plan;
    std::optional<Refusal> problem = plan_descendants(
        document_, *node, operation,
        [&intent](Node& child) { return Geometry::translate_params(child, intent.dx, intent.dy); }, plan);
    if (problem) return refused(Kind::Change, std::move(*problem));
    if (plan.params.empty() && plan.transformed.empty()) {
        return refused(Kind::Change, empty);
    }
    commit_cascade(document_, plan);
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

    if ((*mode == EditMode::Transform || *mode == EditMode::ContainerTransform) && is_container(*payload) &&
        document_.children(*node).empty()) {
        return refused(Kind::Change, empty);
    }

    if (*mode == EditMode::Transform || *mode == EditMode::ContainerTransform) {
        if (!std::holds_alternative<SVGNode>(*payload)) {
            return refused(Kind::Change, fault::TextCannotScale{});
        }

        const Transform& current = std::get<SVGNode>(*payload).transform_attr.transform;
        const std::optional<Transform> scaled_transform = compose(scaled(intent.factor, intent.factor), current);
        if (!scaled_transform) return refused(Kind::Change, fault::InternalError{});
        std::get<SVGNode>(*payload).transform_attr.transform = *scaled_transform;

        Transaction edit = document_.transaction();
        edit.update(*node, std::move(*payload));
        commit_edit(edit);
        return happened(Kind::Change, event::Scaled{named, intent.factor, *mode, 1});
    }

    if (*mode == EditMode::Params) {
        std::optional<Refusal> problem = Geometry::scale_params(*payload, intent.factor);
        if (problem) return refused(Kind::Change, std::move(*problem));

        Transaction edit = document_.transaction();
        edit.update(*node, std::move(*payload));
        commit_edit(edit);
        return happened(Kind::Change, event::Scaled{named, intent.factor, EditMode::Params, 1});
    }

    const Transform operation = scaled(intent.factor, intent.factor);
    CascadePlan plan;
    std::optional<Refusal> problem = plan_descendants(
        document_, *node, operation,
        [&intent](Node& child) { return Geometry::scale_params(child, intent.factor); }, plan);
    if (problem) return refused(Kind::Change, std::move(*problem));
    if (plan.params.empty() && plan.transformed.empty()) {
        return refused(Kind::Change, empty);
    }
    commit_cascade(document_, plan);
    return happened(Kind::Change,
                    event::Scaled{named, intent.factor, EditMode::Cascade,
                                  plan.params.size() + plan.transformed.size()});
}

template<> Outcome Editor::handle<SetCanvas>(const SetCanvas& intent) {
    const std::optional<LengthPercentage> width = present_length_percentage(intent.width);
    const std::optional<LengthPercentage> height = present_length_percentage(intent.height);
    if (width && !is_finite(*width)) {
        return refused(Kind::Change, fault::NonFiniteValue{"width"});
    }
    if (height && !is_finite(*height)) {
        return refused(Kind::Change, fault::NonFiniteValue{"height"});
    }
    if (width && numeric_value(*width) <= 0.0F) {
        return refused(Kind::Change, fault::NonPositiveCanvas{"width"});
    }
    if (height && numeric_value(*height) <= 0.0F) {
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
    if (width) size.width = maybe_length_percentage(*width);
    if (height) size.height = maybe_length_percentage(*height);

    Transaction edit = document_.transaction();
    edit.update(root, std::move(*payload));
    commit_edit(edit);
    // The canvas is what the frame behind the drawing is from here on, so the way back to
    // the canvas before it goes with the history that remembered it. Like `init`, `read`
    // and `save`, this is a point outside the editable history rather than an edit in it.
    document_.clear_history(true);
    return happened(Kind::Change, event::CanvasSet{size.width, size.height});
}
