#include "Planner.hh"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

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

// The routes, one explicit specialization per instruction: the primary template in
// the header is the only thing an instruction with no route can reach, and it fails
// the build naming the type.
template<> Outcome Planner::handle<ShowTree>(const ShowTree& intent) { return show_tree(intent); }
template<> Outcome Planner::handle<Show>(const Show& intent) { return show_node(intent); }
template<> Outcome Planner::handle<Init>(const Init& intent) { return init(intent); }
template<> Outcome Planner::handle<Read>(const Read& intent) { return read(intent); }

template<> Outcome Planner::handle<CreateRect>(const CreateRect& intent) {
    return editor_.translate(ChangeIntent{intent});
}
template<> Outcome Planner::handle<CreateCircle>(const CreateCircle& intent) {
    return editor_.translate(ChangeIntent{intent});
}
template<> Outcome Planner::handle<CreateEllipse>(const CreateEllipse& intent) {
    return editor_.translate(ChangeIntent{intent});
}
template<> Outcome Planner::handle<CreateLine>(const CreateLine& intent) {
    return editor_.translate(ChangeIntent{intent});
}
template<> Outcome Planner::handle<CreateText>(const CreateText& intent) {
    return editor_.translate(ChangeIntent{intent});
}
template<> Outcome Planner::handle<CreateGroup>(const CreateGroup& intent) {
    return editor_.translate(ChangeIntent{intent});
}
template<> Outcome Planner::handle<Delete>(const Delete& intent) { return editor_.translate(ChangeIntent{intent}); }
template<> Outcome Planner::handle<Move>(const Move& intent) { return editor_.translate(ChangeIntent{intent}); }
template<> Outcome Planner::handle<ScaleBy>(const ScaleBy& intent) { return editor_.translate(ChangeIntent{intent}); }
template<> Outcome Planner::handle<Group>(const Group& intent) { return editor_.translate(ChangeIntent{intent}); }
template<> Outcome Planner::handle<Ungroup>(const Ungroup& intent) { return editor_.translate(ChangeIntent{intent}); }
template<> Outcome Planner::handle<AddTo>(const AddTo& intent) { return editor_.translate(ChangeIntent{intent}); }
template<> Outcome Planner::handle<Front>(const Front& intent) { return editor_.translate(ChangeIntent{intent}); }
template<> Outcome Planner::handle<Back>(const Back& intent) { return editor_.translate(ChangeIntent{intent}); }
template<> Outcome Planner::handle<SetCanvas>(const SetCanvas& intent) {
    return editor_.translate(ChangeIntent{intent});
}
template<> Outcome Planner::handle<Undo>(const Undo&) { return editor_.translate(ChangeIntent{Undo{}}); }
template<> Outcome Planner::handle<Redo>(const Redo&) { return editor_.translate(ChangeIntent{Redo{}}); }

template<> Outcome Planner::handle<Save>(const Save& intent) { return save(intent); }

Outcome Planner::dispatch(const Intent& intent) {
    return std::visit([this](const auto& value) { return handle(value); }, intent);
}

Outcome Planner::show_tree(const ShowTree& intent) const {
    const std::optional<Id> node = look_up(document_, intent.node);
    if (!node) return Outcome{false, Kind::View, "there is no node at " + describe(intent.node)};

    TreeView view;
    append_rows(view.rows, document_, *node, 0, true);
    return Outcome{true, Kind::View, {}, std::move(view)};
}

Outcome Planner::show_node(const Show& intent) const {
    const std::optional<Id> node = look_up(document_, intent.node);
    if (!node) return Outcome{false, Kind::View, "there is no node at " + describe(intent.node)};

    const std::optional<Node> payload = document_.node(*node);
    if (!payload) return Outcome{false, Kind::View, "the node went missing"};  // cannot happen: find()
    return Outcome{true, Kind::View, {}, NodeView{*node, *payload, content_of(document_, *node)}};
}

Outcome Planner::init(const Init& intent) {
    document_.reset(intent.width, intent.height);
    return Outcome{true, Kind::Context, "an empty document, with the canvas it was given"};
}

Outcome Planner::read(const Read& intent) const {
    const Status status = document_.open(intent.path);
    return Outcome{status.ok, Kind::Context, status.message};
}

Outcome Planner::save(const Save& intent) {
    const Status status = document_.save(intent.path);
    return Outcome{status.ok, Kind::Context, status.message};
}
