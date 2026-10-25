#include "Planner.hh"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "Exhaustive.hh"
#include "Event.hh"
#include "Intent.hh"
#include "NodeRef.hh"
#include "Outcome.hh"
#include "Refusal.hh"
#include "View.hh"

namespace {

template<typename T>
ElementData element_data_of(const T&) {
    static_assert(always_false<T>, "no view projection for this element payload");
    return Shape{};
}

template<>
ElementData element_data_of<Shape>(const Shape& payload) {
    return payload;
}

template<>
ElementData element_data_of<Container>(const Container& payload) {
    return ContainerShape{payload.specific};
}

template<>
ElementData element_data_of<Text>(const Text& payload) {
    return TextShape{payload.common, payload.specific};
}

// Project one stored payload onto the storage-independent view vocabulary. The SVGNode
// links stay in Model; only the element's named attributes and data cross the boundary.
struct ShapeOf {
    NodeShape operator()(const SVGNode& element) const {
        ElementData data = std::visit(
            [](const auto& payload) -> ElementData {
                using T = std::remove_cvref_t<decltype(payload)>;
                return element_data_of<T>(payload);
            },
            element.data);
        return ElementShape{element.identity_attr, element.paint_attr, std::move(data)};
    }
    NodeShape operator()(const PlainText& text) const { return CharacterView{text.content}; }
    template <class T>
    NodeShape operator()(const T&) const {
        static_assert(always_false<T>, "no view shape for this node payload");
        return CharacterView{};
    }
};

NodeShape shape_of(const Node& payload) { return std::visit(ShapeOf{}, payload); }

// The words of a text element: what the character data under it says. SVG keeps text
// in children rather than in an attribute, and a tree in which every word is a line
// of its own is unreadable, so the two are handed to the front end together.
std::string content_of(const Document& document, Id node) {
    std::string text;
    for (const Id child : document.children(node)) {
        const std::optional<Node> payload = document.node(child);
        if (payload && std::holds_alternative<PlainText>(*payload)) {
            text += std::get<PlainText>(*payload).content;
        }
    }
    return text;
}

// A shown node's parent, when it is not the root. The payload travels with the parent id
// because a view is a snapshot: the front end names the parent from this copy rather than
// reaching back into the document after the read has returned.
std::optional<NodeParent> parent_of(const Document& document, Id node) {
    const std::optional<Id> parent = document.parent(node);
    if (!parent) return std::nullopt;

    const std::optional<Node> payload = document.node(*parent);
    if (!payload) return std::nullopt;
    return NodeParent{*parent, std::get<ElementShape>(shape_of(*payload))};
}

// Reading the tree is the Planner's half of a view: the walk that turns the document
// into the rows a front end can render. The words around them are not its business.
void append_rows(std::vector<TreeRow>& rows, const Document& document, Id node, std::size_t depth, bool last) {
    const std::optional<Node> payload = document.node(node);
    if (!payload) return;
    // Character data is not a node of the tree a user edits: it is part of the text
    // element that holds it, and it travels with it.
    NodeShape shape = shape_of(*payload);
    if (std::holds_alternative<CharacterView>(shape)) return;

    rows.push_back(TreeRow{depth, last, node, std::get<ElementShape>(std::move(shape)), content_of(document, node),
                           document.transform(node)});

    const std::vector<Id> children = document.children(node);
    for (std::size_t index = 0; index < children.size(); ++index) {
        append_rows(rows, document, children[index], depth + 1, index + 1 == children.size());
    }
}

}  // namespace

Planner::Planner(Document& document, Editor& editor, FileSystem& filesystem)
    : document_(document), editor_(editor), filesystem_(filesystem) {}

// The routes that are not the Editor's: the primary template in the header sends every
// change instruction there, so what is left is what the Planner itself answers - the
// views, and which document is being edited. Each of these is short enough to read
// where it is routed, so there is no second place to look for what one of them does.
template<> Outcome Planner::handle<ShowTree>(const ShowTree& intent) {
    if (!document_.has_document()) return no_document(Kind::View);
    const std::optional<Id> node = look_up(document_, intent.node);
    if (!node) return refused(Kind::View, fault::MissingShape{intent.node});

    // The root of the view is the row the front end prints first, and its children are
    // the rows under it - which is why the walk starts at depth 0.
    TreeView view;
    append_rows(view.rows, document_, *node, 0, true);
    return viewed(Kind::View, View{std::move(view)});
}

template<> Outcome Planner::handle<Show>(const Show& intent) {
    if (!document_.has_document()) return no_document(Kind::View);
    const std::optional<Id> node = look_up(document_, intent.node);
    if (!node) return refused(Kind::View, fault::MissingShape{intent.node});

    const std::optional<Node> payload = document_.node(*node);
    if (!payload) return refused(Kind::View, fault::InternalError{});  // cannot happen: look_up
    return viewed(Kind::View, View{NodeView{*node, shape_of(*payload), content_of(document_, *node),
                                            parent_of(document_, *node), document_.transform(*node)}});
}

template<> Outcome Planner::handle<ShowHistory>(const ShowHistory& intent) {
    if (!document_.has_document()) return no_document(Kind::View);
    return viewed(Kind::View, View{editor_.history(intent.limit)});
}

template<> Outcome Planner::handle<Zoom>(const Zoom& intent) {
    if (!std::isfinite(intent.factor) || intent.factor <= 0.0F) {
        return refused(Kind::View, fault::ZoomFactorNotPositive{});
    }
    zoom_ = intent.factor;
    return viewed(Kind::View, ZoomView{zoom_});
}

Outcome Planner::dispatch(const Intent& intent) {
    return std::visit([this](const auto& value) { return handle(value); }, intent);
}
