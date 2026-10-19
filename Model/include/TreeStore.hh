#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include <mvcc_hive/mvcc_hive.hpp>

#include "History.hh"
#include "SVGNode.hh"

// The child-list ends of a node, or nothing when its type cannot hold children.
std::optional<ChildAttr> child_attr_of(const Node& node);
bool set_child_attr(Node& node, const ChildAttr& attr);

// The tree is one MVCC hive of Node records. A node carries its parent and its two
// sibling links, and a node that can hold children carries the two ends of its child
// list. The hive keeps old versions of a record, so a move or a payload edit writes a
// new version instead of moving a payload between slots.
//
// TreeStore is the only place in the project that opens an MVCCHive transaction or
// names its snapshot type. Document uses the viewing API for reads and hands the
// editing API to the Editor.
class TreeStore {
public:
    using Hive = mvcc_hive::MVCCHive<Node>;
    using Snapshot = Hive::Snapshot;
    using Edit = Hive::Transaction;
    static_assert(std::is_same_v<Id, Hive::VirtualId>, "a node handle has to be a hive virtual id");
    static_assert(std::is_same_v<CommitId, Hive::CommitId>, "a commit id has to be the hive's commit id");

    TreeStore();

    // Back to a fresh document: one root element, the canvas it is given (the default
    // for whichever side is missing), and no history at all.
    void reset(std::optional<LengthPercentage> width, std::optional<LengthPercentage> height);

    // --- one user-level edit ---

    // The transaction is constructed by the caller, which is the one object that keeps
    // it alive for the whole edit. Direct initialization from the returned prvalue is
    // what lets a transaction hold the hive's non-movable transaction by value.
    Edit begin();
    // Zero publishes with MVCCHive's automatic id; an empty commit is then no block.
    // A nonzero id is MVCCHive's explicit commit: it must exceed the hive's high-water
    // mark, and even an empty commit advances the checkout. A coordinator passes one
    // explicit id to every hive taking part in the edit.
    void commit(Edit& edit, CommitId commit = 0);
    void rollback(Edit& edit) noexcept;

    // --- history ---

    bool can_undo() const;
    bool can_redo() const;
    // What changed in the visible tree across the checkout being left or entered.
    TouchedNodes undo();
    TouchedNodes redo();

    // How many edits the history holds, and how many of them are applied.
    std::size_t depth() const;
    std::size_t position() const;

    // The commit the checkout stands on, which is what an edit's outcome is filed
    // under. Zero before a reset or a successful open has made a document.
    CommitId commit_id() const;

    // The commit at a zero-based history position. A coordinator uses the same value
    // for every hive that took part in that edit.
    std::optional<CommitId> commit_at(std::size_t position) const;

    // Forget the way back: the current tree stays and becomes the baseline.
    void clear_history();

    // --- viewing the tree at the current history position ---

    Id root() const { return root_; }

    // Whether a live document node lives at that id.
    bool has_node(Id node) const;

    // The node itself, which throws std::out_of_range when no live node is there.
    const Node& record(Id node) const;

    // The node that holds this one, or no_id for the root and for an id that names
    // nothing.
    Id parent(Id node) const;

    std::vector<Id> children(Id parent) const;

    // --- edits, only inside a transaction ---

    // A new node, placed last among the children of its parent.
    Id append_child(Edit& edit, Id parent, Node payload);

    // An existing node, unlinked from wherever it was and placed last among the
    // children of this parent.
    void attach_last(Edit& edit, Id parent, Id node);

    // The same two edits, placed before a sibling instead of at the end: the node lands
    // immediately before `before` among that parent's children, and no_id is the last
    // place again.
    Id insert_at(Edit& edit, Id parent, Id before, Node payload);
    void attach_at(Edit& edit, Id parent, Id before, Id node);

    // A node's payload, with its place in the tree and its child list untouched.
    // Whether the node can hold children is part of what the payload is, so an update
    // cannot change that; a kind change is a removal and a create.
    void update(Edit& edit, Id node, Node payload);

    // A node and everything under it, as one removal.
    void erase_subtree(Edit& edit, Id node);

private:
    using NodeRef = std::reference_wrapper<const Node>;
    enum class Direction { Undo, Redo };

    // The node as the current checkout sees it.
    std::optional<NodeRef> live_node(Id node) const;

    // The virtual ids touched by `commit`, classified from the hive's public diff. The
    // diff entries include neighbours whose links a move changed, not just the node an
    // instruction named. Undo reports the inverse of insert/remove because the event
    // describes the effect of the operation being performed.
    TouchedNodes touched_by(Hive::CommitId commit, Direction direction) const;

    template<typename Patch>
    void patch_node(Edit& edit, Id node, Patch&& patch) {
        const std::optional<NodeRef> found = edit.find(node);
        if (!found) throw std::out_of_range("TreeStore: no live node at this id");

        Node replacement = found->get();
        patch(replacement);
        edit.replace(node, std::move(replacement));
    }

    template<typename Patch>
    void patch_child_attr(Edit& edit, Id node, Patch&& patch) {
        patch_node(edit, node, [&patch](Node& record) {
            const std::optional<ChildAttr> current = child_attr_of(record);
            if (!current) throw std::logic_error("TreeStore: this node cannot hold children");
            ChildAttr replacement = *current;
            patch(replacement);
            set_child_attr(record, replacement);
        });
    }

    void detach(Edit& edit, Id node);

    // The checkout the hive reports and the id recorded beside it are the same state.
    // This is the invariant that keeps a history position and the hive commit from
    // drifting apart after a commit, undo, redo or checkpoint.
    void require_checkout_matches_view() const;

    // The hive, the current checkout snapshot, and the position/depth the front end
    // sees. MVCCHive owns the checkout itself; TreeStore does not keep redo snapshots
    // alive, so a new commit is free to discard the redo tail.
    Hive nodes_;
    std::optional<Snapshot> view_;
    Id root_ = no_id;
    // The commit at each position on the current branch; entry zero is the baseline.
    std::deque<CommitId> checkouts_;
    std::size_t position_ = 0;
};
