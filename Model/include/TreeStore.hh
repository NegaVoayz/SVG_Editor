#pragma once

#include <optional>
#include <utility>
#include <vector>

// The arena the tree lives in: one hive for the nodes themselves, and one for each kind of
// row that puts them in a tree - which node holds a node, where a node's own children end,
// and the links between the siblings themselves. This is Model's storage layer, and the one
// place that talks to SwapDiffHive at all - the Editor writes through
// Document::transaction() and the Planner reads through Document's view API, so no other
// module and no other part of Model has any business with these hives or with their
// transactions.
//
// The split is what keeps a move small. A Node is the whole record: it carries two handles -
// the row that says who holds it, and for an element that can hold children the row its own
// chain is written in - and neither handle changes when the node moves. Moving a node patches
// its parent row and the sibling links around it, so the node - with its identity, its paint
// and its transform - is not copied just to say that it now sits somewhere else. See
// Structure.md.
//
// It sits in include/ rather than src/ so that Document can hold it by value instead of
// through another pointer: the arena of a document is small in itself, it must be laid out
// with the Document it belongs to, and it must be built and destroyed with it.
#include "SwapDiffHive/SwapDiffHive.hh"

#include "SVGNode.hh"

// A child link: the node it holds, and the link of the sibling in front of it. A node's
// children are read from its last link backwards until the chain ends at no_id, which is
// what makes appending a child O(1). See Structure.md.
struct Edge {
    Id child_id;
    Id prev;
};

// The row that says which node holds one node. Every node has one, the root included, whose
// parent is no_id. It is a row of its own rather than a field of the node record because a
// move changes it, and the node record is the one thing a move must not have to write.
struct ParentLink {
    Id parent;  // no_id == the root, which is held by nothing
};

// The row that says where a child-holding node's children end, which is where its chain is
// read from. Only a payload with a ChildAttr has one - the payload keeps the row's handle,
// which is also what makes the capability a fact about the type rather than a claim.
struct ChildLink {
    Id last_child_edge;  // no_id == no children yet
};

// Every edit is a transaction on all four hives under the same diff id, which is what turns
// one user intent into one block of history.
class TreeStore {
public:
    TreeStore();

    // Back to what a fresh arena holds: one root element, the canvas it is given (the
    // default for whichever side is missing), and no history at all.
    void reset(std::optional<LengthPercentage> width, std::optional<LengthPercentage> height);

    void begin(Id diff_id);
    void commit();
    void rollback();
    bool in_transaction() const { return in_transaction_; }

    // Walk every hive to the end of the block with this id. No transaction may be open while
    // this happens.
    void seek(Id diff_id);

    // Forget the history: the tree stays as it is and becomes the baseline. All the hives
    // have to forget together, or they would disagree about where the beginning is.
    void clear_history();

    // --- edits, only inside a transaction ---

    // A new node, placed last among the children of its parent.
    Id append_child(Id parent, Node payload);

    // An existing node, unlinked from wherever it was and placed last among the children of
    // this parent. The payload is not touched: only the rows that say where it sits are.
    void attach_last(Id parent, Id node);

    // The same two edits, placed in front of a sibling instead of at the end: the node lands
    // immediately before `before` among that parent's children, and no_id is the last place
    // again. append_child() and attach_last() are the two calls that always ask for the end,
    // which is the cheap one to write.
    Id insert_at(Id parent, Id before, Node payload);
    void attach_at(Id parent, Id before, Id node);

    // A node's payload, with its place in the tree untouched. Whether the node can hold
    // children is part of what the payload is - it was given its child row by its type - so
    // an update cannot change that; a kind change is an erase and a create.
    void update(Id node, Node payload);

    // A node and everything under it, in one go.
    void erase_subtree(Id node);

    // --- viewing ---

    Id root() const { return root_; }

    // Whether a node lives at that handle, and the node itself, which throws
    // std::out_of_range when it does not. The reference is only good until the next edit: a
    // node moves between slots while one is applied.
    bool has_node(Id node) const;
    const Node& record(Id node) const;

    // The node that holds this one, or no_id for the root and for a handle that names
    // nothing.
    Id parent(Id node) const;

    std::vector<Id> children(Id parent) const;

private:
    void require_transaction() const;

    // The two rows a node owns: the parent row by the handle its record keeps, the child row
    // by the handle the payload keeps.
    Id parent_of(Id node) const;
    Id last_edge_of(Id node) const;
    bool can_hold(Id node) const;
    Id child_row_of(const Node& payload) const;
    void make_child_row(Node& payload);
    bool set_child_row(Node& payload, Id row);
    void set_parent_link(Node& payload, Id row);
    void patch_parent(Id node, Id parent);
    void patch_last_edge(Id node, Id edge);

    void link_last(Id parent, Id child);
    void link_before(Id parent, Id before, Id child);
    Id edge_of(Id parent, Id child) const;
    void unlink(Id node);
    void patch_edge(Id edge, Edge replacement);
    bool has_edge(Id edge) const;

    // The hive only ever moves payloads, never rewrites one in place, so a patch is
    // insert + swap + remove: the patched record takes its own slot back and the record it
    // displaced stays parked in the scratch slot, which is where a rollback restores it from.
    // Every row in this store is a handful of ids, which is the point of the split.
    template<typename T, typename Patch>
    void patch_row(SwapDiffHive<T>& hive, Id row, Patch&& patch) {
        require_transaction();
        T replacement = hive.at(row);
        patch(replacement);

        const Id scratch = hive.insert(std::move(replacement));
        hive.swap(row, scratch);
        hive.remove(scratch);
    }

    SwapDiffHive<Node> nodes_;
    SwapDiffHive<ParentLink> parents_;
    SwapDiffHive<ChildLink> children_;
    SwapDiffHive<Edge> edges_;
    Id root_ = no_id;
    bool in_transaction_ = false;
};
