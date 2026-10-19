#pragma once

#include <cstdint>
#include <vector>

#include "BasicType.hh"

// The sequence number of one commit in a document's hive. Model keeps the value
// meaningful on its own: commits on one branch are consecutive, an edit after undo
// reuses the id of the discarded tail, and clear_history() keeps the checkout's id
// even though the commits below it are gone. Zero means no commit, as before the
// first document exists.
using CommitId = std::uint64_t;

// The id a document-wide edit is filed under. It comes from one process-wide,
// strictly increasing sequence, so it is never reused by a branch, a checkpoint or
// another hive. A hive keeps the local CommitId it used for the same edit; the pair
// is what lets a checkout be resolved across every hive that took part in the edit.
using GlobalCommitId = std::uint64_t;

// The next id in the global sequence. Zero is reserved for "no commit".
[[nodiscard]] GlobalCommitId next_global_commit_id();

// What an undo or redo did to one stored node. The kind is the effect of the operation
// being reported, not the kind of the original forward commit: undoing a create removes
// the node, and undoing a delete restores it.
enum class TouchedKind {
    Insert,
    Update,
    Remove,
};

struct TouchedNode {
    Id node;
    TouchedKind kind;
};

using TouchedNodes = std::vector<TouchedNode>;
