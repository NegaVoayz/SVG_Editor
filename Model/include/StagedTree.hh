#pragma once

#include <cstddef>
#include <vector>

#include "SVGNode.hh"

// A complete tree that has not entered the document yet. Nodes refer to children by
// their index in `nodes`, so parsing can build the value tree without opening a
// transaction. FileSystem produces this value; Model validates and installs it.
struct StagedNode {
    Node payload;
    std::vector<std::size_t> children;
};

struct StagedTree {
    std::vector<StagedNode> nodes;
    std::size_t root = 0;
};
