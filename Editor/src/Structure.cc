// The edits that change the shape of the tree rather than what a node holds: a node that
// goes away, and a node that ends up somewhere else - at one end of its level, under a
// new group, or standing where a group used to.
#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "NodeRef.hh"
#include "Outcome.hh"
#include "Translations.hh"

namespace {

// Whether `candidate` is the ancestor itself or sits somewhere under it. Attaching an
// ancestor to a group built under one of its descendants would make the tree a cycle.
bool inside(const Document& document, Id candidate, Id ancestor) {
    for (Id walk = candidate; walk != no_id; walk = document.parent(walk).value_or(no_id)) {
        if (walk == ancestor) return true;
    }
    return false;
}

// The name a top-level-container error should use: the root's SVG id when a parsed
// document gave it one, and the root reference otherwise.
std::string top_level_name(const Document& document) {
    const std::optional<Node> payload = document.node(document.root());
    if (payload && std::holds_alternative<SVGNode>(*payload)) {
        const std::string& id = std::get<SVGNode>(*payload).identity_attr.id;
        if (!id.empty()) return id;
    }
    return "/";
}

}  // namespace

template<> Outcome Editor::handle<Delete>(const Delete& intent) {
    const Id node = resolve(intent.node);

    Transaction edit = document_.transaction();
    edit.erase(node);
    edit.commit();
    return Outcome{true, Kind::Change, "deleted " + describe(intent.node) + " (#" + std::to_string(node) +
                                           ") and everything under it"};
}

template<> Outcome Editor::handle<AddTo>(const AddTo& intent) {
    const Id parent = resolve_container(intent.parent);
    const Id node = resolve(intent.node);
    if (inside(document_, parent, node)) {
        return refusal(Kind::Change,
                       "error: cannot group a container into its own descendant: " + label(intent.node));
    }

    Transaction edit = document_.transaction();
    edit.attach_last(parent, node);
    edit.commit();
    return Outcome{true, Kind::Change,
                   "moved #" + std::to_string(node) + " to the end of #" + std::to_string(parent)};
}

template<> Outcome Editor::handle<Front>(const Front& intent) {
    const Id node = resolve(intent.node);
    const std::optional<Id> parent = document_.parent(node);
    if (!parent) {
        return refusal(Kind::Change, "the root is the whole document: it has no siblings to stand in front of");
    }

    const std::vector<Id> siblings = document_.children(*parent);
    if (!siblings.empty() && siblings.front() == node) {
        return Outcome{true, Kind::Change, describe(intent.node) + " is already the first child"};
    }

    Transaction edit = document_.transaction();
    edit.attach_at(*parent, siblings.front(), node);
    edit.commit();
    return Outcome{true, Kind::Change,
                   "moved #" + std::to_string(node) + " to the front of #" + std::to_string(*parent)};
}

template<> Outcome Editor::handle<Back>(const Back& intent) {
    const Id node = resolve(intent.node);
    const std::optional<Id> parent = document_.parent(node);
    if (!parent) {
        return refusal(Kind::Change, "the root is the whole document: it has no siblings to stand behind");
    }

    const std::vector<Id> siblings = document_.children(*parent);
    if (!siblings.empty() && siblings.back() == node) {
        return Outcome{true, Kind::Change, describe(intent.node) + " is already the last child"};
    }

    Transaction edit = document_.transaction();
    edit.attach_last(*parent, node);
    edit.commit();
    return Outcome{true, Kind::Change,
                   "moved #" + std::to_string(node) + " to the back of #" + std::to_string(*parent)};
}

template<> Outcome Editor::handle<Group>(const Group& intent) {
    if (intent.nodes.empty()) return refusal(Kind::Change, "a group needs at least one node to hold");
    if (document_.find(intent.id)) {
        return refusal(Kind::Change, "error: id already exists: " + intent.id);
    }

    std::vector<Id> members;
    members.reserve(intent.nodes.size());
    for (const NodeRef& reference : intent.nodes) members.push_back(resolve(reference));

    // The group takes the place of the first node it takes, which means standing in front
    // of the first sibling after that node which is not being taken itself.
    const Id first = members.front();
    const std::optional<Id> parent = document_.parent(first);
    if (!parent) return refusal(Kind::Change, "error: cannot group top-level container: " + top_level_name(document_));

    // The new group is made under `parent`, so taking one of parent's own ancestors would
    // put that container inside a group that is already inside it.
    for (std::size_t index = 0; index < members.size(); ++index) {
        if (inside(document_, *parent, members[index])) {
            return refusal(Kind::Change, "error: cannot group a container into its own descendant: " +
                                             label(intent.nodes[index]));
        }
    }

    const auto taken = [&members](Id node) {
        return std::find(members.begin(), members.end(), node) != members.end();
    };
    Id anchor = no_id;  // nothing stands after them: the end of the list
    bool past_first = false;
    for (const Id sibling : document_.children(*parent)) {
        if (sibling == first) {
            past_first = true;
            continue;
        }
        if (past_first && !taken(sibling)) {
            anchor = sibling;
            break;
        }
    }

    Transaction edit = document_.transaction();
    const Id made = edit.insert_at(*parent, anchor, make_group(intent.id));
    // In the order they were written, so the group reads the way the command did.
    for (const Id member : members) edit.attach_last(made, member);
    edit.commit();
    return Outcome{true, Kind::Change, "created #" + std::to_string(made) + " <g> id=\"" + intent.id + "\" holding " +
                                           std::to_string(members.size()) + " node(s)"};
}

template<> Outcome Editor::handle<Ungroup>(const Ungroup& intent) {
    const std::optional<Id> found = look_up(document_, intent.node);
    if (!found) return refusal(Kind::Change, "error: no such container: " + label(intent.node));
    const Id node = *found;
    if (node == document_.root()) {
        return refusal(Kind::Change, "error: cannot ungroup top-level container: " + top_level_name(document_));
    }

    const std::optional<Node> payload = document_.node(node);
    if (!payload || !is_container(*payload)) {
        return refusal(Kind::Change, "error: cannot ungroup composite shape: " + label(intent.node));
    }

    const std::optional<Id> parent = document_.parent(node);
    if (!parent) return refusal(Kind::Change, "error: cannot ungroup top-level container: " + top_level_name(document_));

    const std::vector<Id> members = document_.children(node);
    // What stood after the group keeps standing after its children: each of them is
    // placed in front of that node, in order, which leaves them in the order they had.
    const std::vector<Id> siblings = document_.children(*parent);
    const auto at = std::find(siblings.begin(), siblings.end(), node);
    const Id anchor = (at != siblings.end() && at + 1 != siblings.end()) ? *(at + 1) : no_id;

    Transaction edit = document_.transaction();
    for (const Id member : members) edit.attach_at(*parent, anchor, member);
    edit.erase(node);
    edit.commit();
    return Outcome{true, Kind::Change, "released " + std::to_string(members.size()) + " node(s) into #" +
                                           std::to_string(*parent) + " and erased " + describe(intent.node)};
}
