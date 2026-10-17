#include "TreeStore.hh"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

using Edit = TreeStore::Hive::Transaction;
using NodeRef = std::reference_wrapper<const Node>;

// The canvas a fresh document starts with, and what goes in for a side an `init` or
// a `canvas` left out. SVG's own default viewport is 300x150 CSS pixels; this
// editor's front end says 800x600 instead, so the number belongs to the document
// rather than to the command that asks for it.
inline const LengthPercentage fresh_canvas_width = Length{800.0F, Length::Unit::UserUnit};
inline const LengthPercentage fresh_canvas_height = Length{600.0F, Length::Unit::UserUnit};

struct Links {
    Id parent;
    Id previous;
    Id next;
};

bool removed(const Node& node) { return std::holds_alternative<Removed>(node); }

// Give an action every element payload of a node: the `data` alternative of an SVG
// element, and nothing for character data or a removed record. The action then asks
// its own question with `if constexpr`, so no kind is named here and no list of them
// exists - the variant is the list.
template<typename Payload, typename Action>
void visit_data(Payload& payload, Action action) {
    std::visit(
        [action](auto& value) {
            if constexpr (Element<std::remove_cvref_t<decltype(value)>>) std::visit(action, value.data);
        },
        payload);
}

Links links_of(const Node& node) {
    return std::visit(
        [](const auto& value) {
            return Links{value.parent_id, value.previous_sibling, value.next_sibling};
        },
        node);
}

void set_links(Node& node, Links links) {
    std::visit(
        [links](auto& value) {
            value.parent_id = links.parent;
            value.previous_sibling = links.previous;
            value.next_sibling = links.next;
        },
        node);
}

std::optional<NodeRef> staged_node(Edit& edit, Id id) {
    const auto found = edit.find(id);
    if (!found || removed(found->get())) return std::nullopt;
    return *found;
}

NodeRef require_staged(Edit& edit, Id id) {
    const std::optional<NodeRef> found = staged_node(edit, id);
    if (!found) throw std::out_of_range("TreeStore: no live node at this id");
    return *found;
}

Links links_in(Edit& edit, Id id) { return links_of(require_staged(edit, id).get()); }

std::optional<ChildAttr> child_attr_in(Edit& edit, Id id) {
    return child_attr_of(require_staged(edit, id).get());
}

// The payload of the root element. The root is part of the baseline, so erasing
// everything leaves an empty document rather than no document.
Node make_root_payload(std::optional<LengthPercentage> width, std::optional<LengthPercentage> height) {
    SVGNode element;
    element.identity_attr = IdentityAttr{"", ""};
    element.data = Container{ChildAttr{no_id, no_id},
                             Container::SVG{width ? std::move(width) : fresh_canvas_width,
                                            height ? std::move(height) : fresh_canvas_height}};
    return Node{std::move(element)};
}

}  // namespace

std::optional<ChildAttr> child_attr_of(const Node& node) {
    std::optional<ChildAttr> result;
    visit_data(
        node,
        [&result](const auto& value) {
            if constexpr (ChildHolder<std::remove_cvref_t<decltype(value)>>) result = value.child_attr;
        });
    return result;
}

bool set_child_attr(Node& node, const ChildAttr& attr) {
    bool placed = false;
    visit_data(
        node,
        [&placed, &attr](auto& value) {
            if constexpr (ChildHolder<std::remove_cvref_t<decltype(value)>>) {
                value.child_attr = attr;
                placed = true;
            }
        });
    return placed;
}

TreeStore::TreeStore() { reset(std::nullopt, std::nullopt); }

void TreeStore::reset(std::optional<LengthPercentage> width, std::optional<LengthPercentage> height) {
    // Forgetting everything means releasing the snapshots too: the guards would keep the
    // old generation alive, and none of those views can be reached from this document.
    checkpoints_.clear();
    view_.reset();
    nodes_.clear();

    // The root is the first virtual id, 0, because no id is reserved in front of it: a
    // link that leads nowhere is no_id. Its history is dropped immediately, which makes
    // it the baseline and leaves commit 1 for the document's first edit.
    auto edit = nodes_.begin_transaction();
    root_ = edit.emplace(make_root_payload(std::move(width), std::move(height)));
    checkpoints_.reserve(1);
    const Snapshot baseline = edit.commit();
    nodes_.clear_history();

    checkpoints_.push_back(baseline);
    view_ = baseline;
    applied_ = 0;
}

TreeStore::Edit TreeStore::begin() {
    rewind_head_to_view();
    return nodes_.begin_transaction();
}

void TreeStore::commit(Edit& edit) {
    const Hive::CommitId before = view_->commit_id();
    // The hive commit is not reversible once published. Make room for its checkpoint
    // before publishing, so the only throwing step is the commit's own prepare phase.
    checkpoints_.reserve(checkpoints_.size() + 1);
    const Snapshot next = edit.commit();
    if (next.commit_id() == before) return;  // an edit that wrote nothing is no block

    checkpoints_.erase(checkpoints_.begin() + static_cast<std::ptrdiff_t>(applied_ + 1), checkpoints_.end());
    checkpoints_.push_back(next);
    applied_ = checkpoints_.size() - 1;
    view_ = next;
}

void TreeStore::rollback(Edit& edit) noexcept { edit.rollback(); }

bool TreeStore::can_undo() const { return applied_ > 0; }
bool TreeStore::can_redo() const { return applied_ + 1 < checkpoints_.size(); }

void TreeStore::undo() {
    if (!can_undo()) return;
    --applied_;
    view_ = checkpoints_[applied_];
}

void TreeStore::redo() {
    if (!can_redo()) return;
    ++applied_;
    view_ = checkpoints_[applied_];
}

std::size_t TreeStore::depth() const { return checkpoints_.empty() ? 0 : checkpoints_.size() - 1; }
std::size_t TreeStore::position() const { return applied_; }

void TreeStore::clear_history() {
    rewind_head_to_view();

    // The head is the tree being kept. Dropping every checkpoint first releases the
    // snapshot horizons, which lets the pruning pass reclaim the versions no longer
    // reachable from that head.
    checkpoints_.reserve(1);
    checkpoints_.clear();
    view_.reset();
    nodes_.clear_history();

    const Snapshot baseline = nodes_.head();
    checkpoints_.push_back(baseline);
    view_ = baseline;
    applied_ = 0;
}

// Bring the hive's head back to the checkpoint the document is showing. The hive's
// commit timeline can only grow at the head, so an edit after an undo first writes the
// visible state as a new head commit. Only ids touched between the two commits are
// rewritten; a row that was created after the visible state becomes a Removed record,
// and one that is visible there is copied back from the snapshot.
void TreeStore::rewind_head_to_view() {
    const Snapshot head = nodes_.head();
    if (head.commit_id() == view_->commit_id()) return;
    if (head.commit_id() < view_->commit_id()) {
        throw std::logic_error("TreeStore: the visible checkpoint is ahead of the hive");
    }

    std::vector<Id> touched;
    for (Hive::CommitId commit = view_->commit_id() + 1; commit <= head.commit_id(); ++commit) {
        for (const auto& diff : nodes_.diffs(commit)) touched.push_back(static_cast<Id>(diff.virtual_id));
    }
    std::sort(touched.begin(), touched.end());
    touched.erase(std::unique(touched.begin(), touched.end()), touched.end());

    auto edit = nodes_.begin_transaction();
    for (const Id id : touched) {
        const auto visible = view_->find(id);
        if (!visible) {
            edit.replace(id, Removed{});
        } else {
            edit.replace(id, visible->get());
        }
    }

    const Snapshot rewound = edit.commit();
    checkpoints_.erase(checkpoints_.begin() + static_cast<std::ptrdiff_t>(applied_ + 1), checkpoints_.end());
    checkpoints_[applied_] = rewound;
    view_ = rewound;
}

bool TreeStore::has_node(Id node) const { return live_node(node).has_value(); }

const Node& TreeStore::record(Id node) const {
    const std::optional<NodeRef> found = live_node(node);
    if (!found) throw std::out_of_range("TreeStore: no live node at this id");
    return found->get();
}

std::optional<TreeStore::NodeRef> TreeStore::live_node(Id node) const {
    if (!view_) return std::nullopt;
    const auto found = view_->find(node);
    if (!found || removed(found->get())) return std::nullopt;
    return *found;
}

Id TreeStore::parent(Id node) const {
    const std::optional<NodeRef> found = live_node(node);
    if (!found) return no_id;
    return links_of(found->get()).parent;
}

std::vector<Id> TreeStore::children(Id parent) const {
    std::vector<Id> result;
    const std::optional<NodeRef> held = live_node(parent);
    if (!held) return result;

    const std::optional<ChildAttr> attr = child_attr_of(held->get());
    if (!attr) return result;

    for (Id child = attr->first_child; child != no_id;) {
        const std::optional<NodeRef> node = live_node(child);
        if (!node) throw std::logic_error("TreeStore: a child link names no live node");
        result.push_back(child);
        child = links_of(node->get()).next;
    }
    return result;
}

Id TreeStore::append_child(Edit& edit, Id parent, Node payload) {
    return insert_at(edit, parent, no_id, std::move(payload));
}

void TreeStore::attach_last(Edit& edit, Id parent, Id node) {
    attach_at(edit, parent, no_id, node);
}

Id TreeStore::insert_at(Edit& edit, Id parent, Id before, Node payload) {
    if (removed(payload)) throw std::invalid_argument("TreeStore: a removed record cannot be inserted");

    const NodeRef holder = require_staged(edit, parent);
    const std::optional<ChildAttr> attr = child_attr_of(holder.get());
    if (!attr) throw std::logic_error("TreeStore: this node cannot hold children");

    Id previous = attr->last_child;
    if (before != no_id) {
        const Links anchor = links_in(edit, before);
        if (anchor.parent != parent) {
            throw std::invalid_argument("TreeStore: the node to place in front of is not a child of that parent");
        }
        previous = anchor.previous;
    }

    set_links(payload, Links{parent, previous, before});
    const Id child = edit.emplace(std::move(payload));

    if (previous == no_id && before == no_id) {
        patch_child_attr(edit, parent, [child](ChildAttr& ends) {
            ends.first_child = child;
            ends.last_child = child;
        });
    } else {
        if (previous != no_id) {
            patch_node(edit, previous, [child](Node& node) {
                Links links = links_of(node);
                links.next = child;
                set_links(node, links);
            });
        } else {
            patch_child_attr(edit, parent, [child](ChildAttr& ends) { ends.first_child = child; });
        }

        if (before != no_id) {
            patch_node(edit, before, [child](Node& node) {
                Links links = links_of(node);
                links.previous = child;
                set_links(node, links);
            });
        } else {
            patch_child_attr(edit, parent, [child](ChildAttr& ends) { ends.last_child = child; });
        }
    }
    return child;
}

void TreeStore::attach_at(Edit& edit, Id parent, Id before, Id node) {
    if (node == root_) throw std::logic_error("TreeStore: the root cannot be attached to anything");
    if (parent == node) throw std::logic_error("TreeStore: a node cannot hold itself");
    if (before == node) throw std::logic_error("TreeStore: a node cannot be placed in front of itself");

    const NodeRef holder = require_staged(edit, parent);
    if (!child_attr_of(holder.get())) throw std::logic_error("TreeStore: this node cannot hold children");
    static_cast<void>(require_staged(edit, node));

    // The place a node moves to must not be inside the node itself: the parent
    // chain of the new parent is what would have to contain it.
    for (Id walk = parent; walk != no_id; walk = links_in(edit, walk).parent) {
        if (walk == node) throw std::logic_error("TreeStore: a node cannot be moved inside itself");
    }

    if (before != no_id) {
        const Links anchor = links_in(edit, before);
        if (anchor.parent != parent) {
            throw std::invalid_argument("TreeStore: the node to place in front of is not a child of that parent");
        }
    }

    detach(edit, node);

    // Read the target after the detach: if the node came from the same level, removing
    // it changed either the parent's last child or the anchor's previous link.
    const std::optional<ChildAttr> ends = child_attr_in(edit, parent);
    Id previous = ends->last_child;
    if (before != no_id) previous = links_in(edit, before).previous;

    patch_node(edit, node, [parent, previous, before](Node& record) {
        set_links(record, Links{parent, previous, before});
    });

    if (previous == no_id && before == no_id) {
        patch_child_attr(edit, parent, [node](ChildAttr& attr) {
            attr.first_child = node;
            attr.last_child = node;
        });
    } else {
        if (previous != no_id) {
            patch_node(edit, previous, [node](Node& record) {
                Links links = links_of(record);
                links.next = node;
                set_links(record, links);
            });
        } else {
            patch_child_attr(edit, parent, [node](ChildAttr& attr) { attr.first_child = node; });
        }

        if (before != no_id) {
            patch_node(edit, before, [node](Node& record) {
                Links links = links_of(record);
                links.previous = node;
                set_links(record, links);
            });
        } else {
            patch_child_attr(edit, parent, [node](ChildAttr& attr) { attr.last_child = node; });
        }
    }
}

void TreeStore::detach(Edit& edit, Id node) {
    const Links link = links_in(edit, node);
    if (link.parent == no_id) throw std::logic_error("TreeStore: this node is not linked under anything");

    if (link.previous == no_id && link.next == no_id) {
        patch_child_attr(edit, link.parent, [](ChildAttr& attr) {
            attr.first_child = no_id;
            attr.last_child = no_id;
        });
    } else {
        if (link.previous != no_id) {
            patch_node(edit, link.previous, [next = link.next](Node& record) {
                Links links = links_of(record);
                links.next = next;
                set_links(record, links);
            });
        } else {
            patch_child_attr(edit, link.parent, [next = link.next](ChildAttr& attr) { attr.first_child = next; });
        }

        if (link.next != no_id) {
            patch_node(edit, link.next, [previous = link.previous](Node& record) {
                Links links = links_of(record);
                links.previous = previous;
                set_links(record, links);
            });
        } else {
            patch_child_attr(edit, link.parent, [previous = link.previous](ChildAttr& attr) { attr.last_child = previous; });
        }
    }
}

void TreeStore::update(Edit& edit, Id node, Node payload) {
    const NodeRef stored = require_staged(edit, node);
    if (removed(payload)) throw std::invalid_argument("TreeStore: a live node cannot be updated to a removed one");

    const std::optional<ChildAttr> stored_children = child_attr_of(stored.get());
    const std::optional<ChildAttr> new_children = child_attr_of(payload);
    if (stored_children.has_value() != new_children.has_value()) {
        throw std::logic_error("TreeStore: an update cannot change whether a node holds children");
    }

    // The node's place and child list belong to the node, not to the payload that
    // happens to be in it.
    set_links(payload, links_of(stored.get()));
    if (stored_children) set_child_attr(payload, *stored_children);
    edit.replace(node, std::move(payload));
}

void TreeStore::erase_subtree(Edit& edit, Id node) {
    if (node == root_) throw std::logic_error("TreeStore: the root cannot be erased");
    static_cast<void>(require_staged(edit, node));

    // The node leaves its level first; its descendants keep their own links until they
    // are replaced, which is what lets the walk read them as one tree.
    detach(edit, node);

    std::vector<Id> pending{node};
    std::vector<Id> doomed;
    while (!pending.empty()) {
        const Id current = pending.back();
        pending.pop_back();

        const NodeRef found = require_staged(edit, current);
        doomed.push_back(current);
        if (const std::optional<ChildAttr> children = child_attr_of(found.get())) {
            for (Id child = children->first_child; child != no_id;) {
                const NodeRef child_node = require_staged(edit, child);
                pending.push_back(child);
                child = links_of(child_node.get()).next;
            }
        }
    }

    for (const Id id : doomed) edit.replace(id, Removed{});
}
