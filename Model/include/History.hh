#pragma once

#include <vector>

#include "BasicType.hh"

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
