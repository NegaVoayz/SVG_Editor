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

// How many nodes a delete takes with the node itself. A leaf says nothing about
// descendants; a container reports the size of the subtree it removes.
std::size_t descendant_count(const Document& document, Id node) {
    std::size_t count = 0;
    std::vector<Id> pending = document.children(node);
    while (!pending.empty()) {
        const Id current = pending.back();
        pending.pop_back();
        ++count;
        for (const Id child : document.children(current)) pending.push_back(child);
    }
    return count;
}

}  // namespace

template<> Outcome Editor::handle<Delete>(const Delete& intent) {
    const std::expected<Id, Refusal> node = resolve(intent.node);
    if (!node) return refused(Kind::Change, node.error());
    if (*node == document_.root()) return refused(Kind::Change, fault::RootCannotBeErased{});

    const std::size_t removed_below = descendant_count(document_, *node);
    Transaction edit = document_.transaction();
    edit.erase(*node);
    edit.commit();
    return happened(Kind::Change, event::Deleted{*node, intent.node, removed_below});
}

template<> Outcome Editor::handle<AddTo>(const AddTo& intent) {
    const std::expected<Id, Refusal> parent = resolve_container(intent.parent);
    if (!parent) return refused(Kind::Change, parent.error());
    const std::expected<Id, Refusal> node = resolve(intent.node);
    if (!node) return refused(Kind::Change, node.error());
    if (inside(document_, *parent, *node)) {
        return refused(Kind::Change, fault::CannotGroupIntoOwnDescendant{intent.node});
    }

    // The child chain is SVG's paint order: the last child is painted on top.
    Transaction edit = document_.transaction();
    edit.attach_last(*parent, *node);
    edit.commit();
    return happened(Kind::Change, event::AddedTo{*node, *parent});
}

// SVG paints children in document order, so the last child is the front. `front` and
// `back` name the two ends of the same child chain the tree prints.
template<> Outcome Editor::handle<Front>(const Front& intent) {
    const std::expected<Id, Refusal> node = resolve(intent.node);
    if (!node) return refused(Kind::Change, node.error());
    const std::optional<Id> parent = document_.parent(*node);
    if (!parent) {
        return refused(Kind::Change, fault::RootHasNoSiblings{});
    }

    const std::vector<Id> siblings = document_.children(*parent);
    if (!siblings.empty() && siblings.back() == *node) {
        return happened(Kind::Change, event::AlreadyFront{intent.node});
    }

    Transaction edit = document_.transaction();
    edit.attach_last(*parent, *node);
    edit.commit();
    return happened(Kind::Change, event::MovedToFront{*node, *parent});
}

template<> Outcome Editor::handle<Back>(const Back& intent) {
    const std::expected<Id, Refusal> node = resolve(intent.node);
    if (!node) return refused(Kind::Change, node.error());
    const std::optional<Id> parent = document_.parent(*node);
    if (!parent) {
        return refused(Kind::Change, fault::RootHasNoSiblings{});
    }

    const std::vector<Id> siblings = document_.children(*parent);
    if (!siblings.empty() && siblings.front() == *node) {
        return happened(Kind::Change, event::AlreadyBack{intent.node});
    }

    Transaction edit = document_.transaction();
    edit.attach_at(*parent, siblings.front(), *node);
    edit.commit();
    return happened(Kind::Change, event::MovedToBack{*node, *parent});
}

template<> Outcome Editor::handle<Group>(const Group& intent) {
    if (intent.nodes.empty()) return refused(Kind::Change, fault::EmptyGroup{});
    if (document_.find(intent.id)) {
        return refused(Kind::Change, fault::DuplicateId{intent.id});
    }

    std::vector<Id> members;
    members.reserve(intent.nodes.size());
    for (const NodeRef& reference : intent.nodes) {
        const std::expected<Id, Refusal> member = resolve(reference);
        if (!member) return refused(Kind::Change, member.error());
        members.push_back(*member);
    }

    // The group takes the place of the first node it takes, which means standing before
    // the first sibling after that node which is not being taken itself.
    const Id first = members.front();
    const std::optional<Id> parent = document_.parent(first);
    if (!parent) {
        return refused(Kind::Change, fault::CannotGroupTopLevelContainer{top_level_name(document_)});
    }

    // The new group is made under `parent`, so taking one of parent's own ancestors would
    // put that container inside a group that is already inside it.
    for (std::size_t index = 0; index < members.size(); ++index) {
        if (inside(document_, *parent, members[index])) {
            return refused(Kind::Change, fault::CannotGroupIntoOwnDescendant{intent.nodes[index]});
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
    return happened(Kind::Change, event::Grouped{made, intent.id, *parent, members.size()});
}

template<> Outcome Editor::handle<Ungroup>(const Ungroup& intent) {
    const std::optional<Id> found = look_up(document_, intent.node);
    if (!found) return refused(Kind::Change, fault::MissingContainer{intent.node});
    const Id node = *found;
    if (node == document_.root()) {
        return refused(Kind::Change, fault::CannotUngroupTopLevelContainer{top_level_name(document_)});
    }

    const std::optional<Node> payload = document_.node(node);
    if (!payload || !is_container(*payload)) {
        return refused(Kind::Change, fault::CannotUngroupCompositeShape{intent.node});
    }

    const std::optional<Id> parent = document_.parent(node);
    if (!parent) {
        return refused(Kind::Change, fault::CannotUngroupTopLevelContainer{top_level_name(document_)});
    }

    const std::vector<Id> members = document_.children(node);
    // What stood after the group keeps standing after its children: each of them is
    // placed before that node, in order, which leaves them in the order they had.
    const std::vector<Id> siblings = document_.children(*parent);
    const auto at = std::find(siblings.begin(), siblings.end(), node);
    const Id anchor = (at != siblings.end() && at + 1 != siblings.end()) ? *(at + 1) : no_id;

    Transaction edit = document_.transaction();
    for (const Id member : members) edit.attach_at(*parent, anchor, member);
    edit.erase(node);
    edit.commit();
    return happened(Kind::Change, event::Ungrouped{node, intent.node, *parent, members.size()});
}
