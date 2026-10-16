#include "Planner.hh"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

// Reading the tree is the Planner's half of a view: the walk that turns the document
// into the rows a front end can render. The words around them are not its business.
void append_rows(std::vector<TreeRow>& rows, const Document& document, Id node, std::size_t depth) {
    const std::optional<Node> payload = document.node(node);
    if (!payload) return;

    rows.push_back(TreeRow{depth, node, *payload});
    for (const Id child : document.children(node)) append_rows(rows, document, child, depth + 1);
}

}  // namespace

Planner::Planner(Document& document, Editor& editor) : document_(document), editor_(editor) {}

// The routes, one explicit specialization per instruction: the primary template in
// the header is the only thing an instruction with no route can reach, and it fails
// the build naming the type.
template<> Outcome Planner::handle<ShowTree>(const ShowTree&) { return show_tree(); }
template<> Outcome Planner::handle<Find>(const Find& intent) { return show_find(intent); }

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

template<> Outcome Planner::handle<Open>(const Open& intent) { return open(intent); }
template<> Outcome Planner::handle<Save>(const Save& intent) { return save(intent); }

Outcome Planner::dispatch(const Intent& intent) {
    return std::visit([this](const auto& value) { return handle(value); }, intent);
}

Outcome Planner::show_tree() const {
    TreeView view;
    append_rows(view.rows, document_, document_.root(), 0);
    return Outcome{true, Kind::View, {}, std::move(view)};
}

Outcome Planner::show_find(const Find& intent) const {
    const std::optional<Id> found = document_.find(intent.svg_id);
    if (!found) return Outcome{false, Kind::View, "no node has the id \"" + intent.svg_id + "\""};

    const std::optional<Node> payload = document_.node(*found);
    if (!payload) return Outcome{false, Kind::View, "the node went missing"};  // cannot happen: find()
    return Outcome{true, Kind::View, {}, NodeView{*found, *payload}};
}

Outcome Planner::open(const Open& intent) const {
    const Status status = document_.open(intent.path);
    return Outcome{status.ok, Kind::Context, status.message};
}

Outcome Planner::save(const Save& intent) {
    const Status status = document_.save(intent.path);
    return Outcome{status.ok, Kind::Context, status.message};
}
