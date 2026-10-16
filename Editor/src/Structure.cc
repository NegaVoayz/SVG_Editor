// The edits that change the shape of the tree rather than what a node holds: a node that
// goes away, and a node that ends up somewhere else - at one end of its level, under a
// new group, or standing where a group used to.
#include <algorithm>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "NodeRef.hh"
#include "Outcome.hh"
#include "Translations.hh"

template<> Outcome Editor::handle<Delete>(const Delete& intent) {
    const Id node = resolve(intent.node);

    Transaction edit = document_.transaction();
    edit.erase(node);
    edit.commit();
    return Outcome{true, Kind::Change, "deleted " + describe(intent.node) + " (#" + std::to_string(node) +
                                           ") and everything under it"};
}

template<> Outcome Editor::handle<AddTo>(const AddTo& intent) {
    const Id parent = resolve(intent.parent);
    const Id node = resolve(intent.node);

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
        return refusal(Kind::Change, "an element is already called \"" + intent.id + "\"");
    }

    std::vector<Id> members;
    members.reserve(intent.nodes.size());
    for (const NodeRef& reference : intent.nodes) members.push_back(resolve(reference));

    // The group takes the place of the first node it takes, which means standing in front
    // of the first sibling after that node which is not being taken itself.
    const Id first = members.front();
    const std::optional<Id> parent = document_.parent(first);
    if (!parent) return refusal(Kind::Change, "the root cannot be put inside a group");

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
    const Id node = resolve(intent.node);
    if (node == document_.root()) {
        return refusal(Kind::Change, "the root is the document itself and cannot be ungrouped");
    }

    const std::optional<Node> payload = document_.node(node);
    if (!payload) return refusal(Kind::Change, "no node lives at that handle");
    if (!std::holds_alternative<SVGNode>(*payload) ||
        !std::holds_alternative<Container>(std::get<SVGNode>(*payload).data)) {
        return refusal(Kind::Change,
                       "only a container can be ungrouped: a shape has nothing inside it, and a text element holds "
                       "its words rather than children that could stand where it stands");
    }

    const std::optional<Id> parent = document_.parent(node);
    if (!parent) return refusal(Kind::Change, "the root is the document itself and cannot be ungrouped");

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
