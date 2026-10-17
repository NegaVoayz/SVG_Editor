#pragma once

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

#include "SVGNode.hh"

// What a read hands back: the document as data rather than as a sentence. The ui layer is
// what turns it into text, because text is its business in both directions of the boundary
// - it parses what a user wrote, and it writes what a user reads.
//
// The payloads are copies. A view is a snapshot of the tree as it stands, and it has to
// outlive the checkpoint or transaction that produced it, so nothing here can be a
// reference into the arena.
//
// `text` is a text element's words, read out of the character data under it. SVG keeps them
// in a child node rather than in an attribute, but a tree in which every word is a line of
// its own is unreadable, so the two arrive together and the words are printed inside the
// element that holds them.
struct TreeRow {
    std::size_t depth;
    bool last;  // whether this node is the last child of its parent, which is what a tree drawing branches on
    Id node;
    Node payload;
    std::string text;
};
struct TreeView { std::vector<TreeRow> rows; };
struct NodeView {
    Id node;
    Node payload;
    std::string text;
};

// One of the two reads, or nothing: an outcome that is a change or a failure carries its
// sentence instead of a view.
using View = std::variant<std::monostate, TreeView, NodeView>;
