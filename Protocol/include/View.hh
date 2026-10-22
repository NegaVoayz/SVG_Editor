#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Event.hh"
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
    // The element's composed transform matrix; identity when it carries none.
    Transform transform = {};
};
struct TreeView { std::vector<TreeRow> rows; };
// Where a shown node hangs. The root has no parent, so this is absent there; every
// other node has one, and the front end may name it beside the node's own heading.
struct NodeParent {
    Id node;
    Node payload;
};
struct NodeView {
    Id node;
    Node payload;
    std::string text;
    std::optional<NodeParent> parent = std::nullopt;
    Transform transform = {};
};

// A window of the edits since the last context change. Entries stay in chronological
// order; a front end showing the newest first reverses them. A checkout moves to one of
// these, so the list is also its operand table.
// The baseline is the state just after the context change. It is deliberately not an
// edit entry because it has no edit outcome, but its id is carried here so both history
// rendering and checkout can show and return to the first retained commit.
struct HistoryView {
    CommitId baseline = 0;
    CommitId head = 0;
    std::size_t position = 0;
    std::size_t depth = 0;
    std::vector<event::HistoryEntry> entries;
};

// One of the two reads, or nothing: an outcome that is a change or a failure carries its
// sentence instead of a view.
using View = std::variant<std::monostate, TreeView, NodeView, HistoryView>;
