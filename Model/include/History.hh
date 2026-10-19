#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "BasicType.hh"

// The id of one commit in an MVCCHive. An automatic commit takes the parent's id
// plus one; an explicit commit may skip values, and an explicit empty commit still
// advances the checkout. Zero means no commit, as before the first document exists.
using CommitId = std::uint64_t;

// The coordinator's sequence for one process. It hands one id to every hive taking
// part in an edit, so their native MVCCHive commit ids are the same checkout id.
[[nodiscard]] CommitId next_commit_id();

// Keep the coordinator ahead of an id a hive issued without asking the coordinator.
void observe_commit_id(CommitId commit);

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

// The result of moving the checkout to a commit in the current history branch.
struct HistoryCheckout {
    std::size_t position;
    TouchedNodes touched;
};
