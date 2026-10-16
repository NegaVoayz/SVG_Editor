#include "Planner.hh"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "Intent.hh"
#include "NodeRef.hh"
#include "Outcome.hh"
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

// Reading the tree is the Planner's half of a view: the walk that turns the document
// into the rows a front end can render. The words around them are not its business.
void append_rows(std::vector<TreeRow>& rows, const Document& document, Id node, std::size_t depth, bool last) {
    const std::optional<Node> payload = document.node(node);
    if (!payload) return;
    // Character data is not a node of the tree a user edits: it is part of the text
    // element that holds it, and it travels with it.
    if (std::holds_alternative<PlainText>(*payload)) return;

    rows.push_back(TreeRow{depth, last, node, *payload, content_of(document, node)});

    const std::vector<Id> children = document.children(node);
    for (std::size_t index = 0; index < children.size(); ++index) {
        append_rows(rows, document, children[index], depth + 1, index + 1 == children.size());
    }
}

}  // namespace

Planner::Planner(Document& document, Editor& editor) : document_(document), editor_(editor) {}

// The routes that are not the Editor's: the primary template in the header sends every
// change instruction there, so what is left is what the Planner itself answers - the
// views, and which document is being edited. Each of these is short enough to read
// where it is routed, so there is no second place to look for what one of them does.
template<> Outcome Planner::handle<ShowTree>(const ShowTree& intent) {
    const std::optional<Id> node = look_up(document_, intent.node);
    if (!node) return refusal(Kind::View, "there is no node at " + describe(intent.node));

    // The root of the view is the row the front end prints first, and its children are
    // the rows under it - which is why the walk starts at depth 0.
    TreeView view;
    append_rows(view.rows, document_, *node, 0, true);
    return Outcome{true, Kind::View, {}, std::move(view)};
}

template<> Outcome Planner::handle<Show>(const Show& intent) {
    const std::optional<Id> node = look_up(document_, intent.node);
    if (!node) return refusal(Kind::View, "there is no node at " + describe(intent.node));

    const std::optional<Node> payload = document_.node(*node);
    if (!payload) return refusal(Kind::View, "the node went missing");  // cannot happen: look_up
    return Outcome{true, Kind::View, {}, NodeView{*node, *payload, content_of(document_, *node)}};
}

// `init` replaces the document, file and all, rather than editing the tree in it: the
// canvas an instruction may carry comes with the new root, and there is nothing left of
// the old document to undo back into.
template<> Outcome Planner::handle<Init>(const Init& intent) {
    document_.reset(intent.width, intent.height);
    return Outcome{true, Kind::Context, "an empty document, with the canvas it was given"};
}

template<> Outcome Planner::handle<Read>(const Read& intent) {
    const Status status = document_.open(intent.path);
    return Outcome{status.ok, Kind::Context, status.message};
}

// A save with no path writes where the document was read from, which is the Model's to
// remember: the front end never sees a path it did not write itself.
template<> Outcome Planner::handle<Save>(const Save& intent) {
    const Status status = document_.save(intent.path);
    return Outcome{status.ok, Kind::Context, status.message};
}

Outcome Planner::dispatch(const Intent& intent) {
    return std::visit([this](const auto& value) { return handle(value); }, intent);
}
