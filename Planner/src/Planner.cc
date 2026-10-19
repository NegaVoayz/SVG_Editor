#include "Planner.hh"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "Event.hh"
#include "Intent.hh"
#include "NodeRef.hh"
#include "Outcome.hh"
#include "Refusal.hh"
#include "Status.hh"
#include "View.hh"

namespace {

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
    return NodeParent{*parent, *payload};
}

// The root's canvas, when it has one. `init` reports the size it actually created,
// including the side it filled in with the default.
std::optional<Container::SVG> canvas_of(const Document& document) {
    const std::optional<Node> payload = document.node(document.root());
    if (!payload || !std::holds_alternative<SVGNode>(*payload)) return std::nullopt;
    const SVGNode& element = std::get<SVGNode>(*payload);
    if (!std::holds_alternative<Container>(element.data)) return std::nullopt;
    const Container& container = std::get<Container>(element.data);
    if (!std::holds_alternative<Container::SVG>(container.specific)) return std::nullopt;
    return std::get<Container::SVG>(container.specific);
}

// Reading the tree is the Planner's half of a view: the walk that turns the document
// into the rows a front end can render. The words around them are not its business.
void append_rows(std::vector<TreeRow>& rows, const Document& document, Id node, std::size_t depth, bool last) {
    const std::optional<Node> payload = document.node(node);
    if (!payload) return;
    // Character data is not a node of the tree a user edits: it is part of the text
    // element that holds it, and it travels with it.
    if (std::holds_alternative<PlainText>(*payload)) return;

    rows.push_back(TreeRow{depth, last, node, *payload, content_of(document, node), document.transform(node)});

    const std::vector<Id> children = document.children(node);
    for (std::size_t index = 0; index < children.size(); ++index) {
        append_rows(rows, document, children[index], depth + 1, index + 1 == children.size());
    }
}

// The file layer's structured status, translated into the same result vocabulary every
// other route uses. The translation is a visitor rather than a chain over codes: a new
// status is a build error here until it has an answer.
struct OpenStatus {
    Kind kind;
    std::string path;
    std::size_t elements;

    Outcome operator()(const model_status::Ok&) const {
        return happened(kind, event::Opened{path, {}, elements});
    }
    Outcome operator()(const model_status::Opened& opened) const {
        return happened(kind, event::Opened{path, opened.assigned_ids, elements});
    }
    Outcome operator()(const model_status::NoDocument&) const { return refused(kind, fault::NoDocument{}); }
    Outcome operator()(const model_status::CannotReadFile& problem) const {
        return refused(kind, fault::CannotReadFile{problem.path});
    }
    Outcome operator()(const model_status::InvalidSvg& problem) const {
        return refused(kind, fault::InvalidSvg{problem.diagnostic});
    }
    Outcome operator()(const model_status::MissingSourcePath&) const {
        return refused(kind, fault::InternalError{});
    }
    Outcome operator()(const model_status::CannotWriteFile&) const {
        return refused(kind, fault::InternalError{});
    }
};

struct SaveStatus {
    Kind kind;
    std::string path;
    std::size_t elements;

    Outcome operator()(const model_status::Ok&) const { return happened(kind, event::Saved{path, elements}); }
    Outcome operator()(const model_status::NoDocument&) const { return refused(kind, fault::NoDocument{}); }
    Outcome operator()(const model_status::Opened&) const { return refused(kind, fault::InternalError{}); }
    Outcome operator()(const model_status::MissingSourcePath&) const {
        return refused(kind, fault::MissingSourcePath{});
    }
    Outcome operator()(const model_status::CannotWriteFile& problem) const {
        return refused(kind, fault::CannotWriteFile{problem.path});
    }
    Outcome operator()(const model_status::CannotReadFile&) const {
        return refused(kind, fault::InternalError{});
    }
    Outcome operator()(const model_status::InvalidSvg&) const {
        return refused(kind, fault::InternalError{});
    }
};

}  // namespace

Planner::Planner(Document& document, Editor& editor) : document_(document), editor_(editor) {}

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
    return viewed(Kind::View, View{NodeView{*node, *payload, content_of(document_, *node),
                                            parent_of(document_, *node), document_.transform(*node)}});
}

// `init` replaces the document, file and all, rather than editing the tree in it: the
// canvas an instruction may carry comes with the new root, and there is nothing left of
// the old document to undo back into.
template<> Outcome Planner::handle<Init>(const Init& intent) {
    if (intent.width && !is_finite(*intent.width)) {
        return refused(Kind::Context, fault::NonFiniteValue{"width"});
    }
    if (intent.height && !is_finite(*intent.height)) {
        return refused(Kind::Context, fault::NonFiniteValue{"height"});
    }
    if (intent.width && numeric_value(*intent.width) <= 0.0F) {
        return refused(Kind::Context, fault::NonPositiveCanvas{"width"});
    }
    if (intent.height && numeric_value(*intent.height) <= 0.0F) {
        return refused(Kind::Context, fault::NonPositiveCanvas{"height"});
    }

    document_.reset(intent.width, intent.height);
    const std::optional<Container::SVG> canvas = canvas_of(document_);
    if (!canvas) return refused(Kind::Context, fault::InternalError{});
    return happened(Kind::Context, event::Initialized{canvas->width, canvas->height});
}

template<> Outcome Planner::handle<Read>(const Read& intent) {
    const Status status = document_.open(intent.path);
    return std::visit(OpenStatus{Kind::Context, intent.path, document_.element_count()}, status);
}

// A save with no path writes where the document was read from, which is the Model's to
// remember: the front end never sees a path it did not write itself.
template<> Outcome Planner::handle<Save>(const Save& intent) {
    if (!document_.has_document()) return no_document(Kind::Context);
    const Status status = document_.save(intent.path);
    const std::string target = intent.path ? *intent.path : document_.source_path();
    return std::visit(SaveStatus{Kind::Context, target, document_.element_count()}, status);
}

Outcome Planner::dispatch(const Intent& intent) {
    return std::visit([this](const auto& value) { return handle(value); }, intent);
}
