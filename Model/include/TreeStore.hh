#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// The arena the tree lives in: one hive for the nodes, one for the child links.
// This is Model's storage layer, and the one place that talks to SwapDiffHive at
// all - the Editor writes through Document::transaction() and the Planner reads
// through Document's view API, so no other module and no other part of Model has
// any business with these hives or with their transactions.
//
// It sits in include/ rather than src/ so that Document can hold it by value
// instead of through another pointer: the arena of a document is small in itself,
// it must be laid out with the Document it belongs to, and it must be built and
// destroyed with it.
#include "SwapDiffHive/SwapDiffHive.hh"

#include "SVGNode.hh"

// A child link: the node it holds, and the link of the sibling in front of it. A
// node's children are read from its last link backwards until the chain ends at
// no_id, which is what makes appending a child O(1). See Structure.md.
struct Edge {
    Id child_id;
    Id prev;
};

// What the node hive stores: the SVG payload plus the tree links that are not SVG
// data. The hive moves payloads between slots, so a record may never point at its
// own storage, and a record is replaced as a whole instead of rewritten.
struct NodeRecord {
    Node payload;
    Id parent;           // no_id == the root, which has no parent
    Id last_child_edge;  // no_id == no children yet
};

// Every edit is a transaction on both hives under the same diff id, which is what
// turns one user intent into one block of history.
class TreeStore {
public:
    TreeStore();

    void begin(Id diff_id);
    void commit();
    void rollback();
    bool in_transaction() const { return in_transaction_; }

    // Walk both hives to the end of the block with this id. No transaction may be
    // open while this happens.
    void seek(Id diff_id);

    // --- edits, only inside a transaction ---

    // A new node, placed last among the children of its parent.
    Id append_child(Id parent, Node payload);

    // An existing node, unlinked from wherever it was and placed last among the
    // children of this parent.
    void attach_last(Id parent, Id node);

    // A node's payload, with its place in the tree untouched.
    void update(Id node, Node payload);

    // A node and everything under it, in one go.
    void erase_subtree(Id node);

    // --- viewing ---

    Id root() const { return root_; }

    // Whether a node lives at that handle, and the record itself, which throws
    // std::out_of_range when it does not. The reference is only good until the
    // next edit: a payload moves between slots while one is applied.
    bool has_node(Id node) const;
    const NodeRecord& record(Id node) const;

    std::vector<Id> children(Id parent) const;

private:
    void require_transaction() const;
    void link_last(Id parent, Id child);
    void unlink(Id node);
    void patch_edge(Id edge, Edge replacement);
    bool has_edge(Id edge) const;

    // The hive only ever moves payloads, never rewrites one in place, so a patch
    // is insert + swap + remove: the patched record takes its own slot back and
    // the record it displaced stays parked in the scratch slot, which is where a
    // rollback restores it from.
    template<typename Patch>
    void patch_node(Id node, Patch&& patch) {
        require_transaction();
        NodeRecord replacement = record(node);
        patch(replacement);

        const Id scratch = nodes_.insert(std::move(replacement));
        nodes_.swap(node, scratch);
        nodes_.remove(scratch);
    }

    SwapDiffHive<NodeRecord> nodes_;
    SwapDiffHive<Edge> edges_;
    Id root_ = no_id;
    bool in_transaction_ = false;
};
