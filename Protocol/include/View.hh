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
// Tree and node payloads are copies so they can outlive the checkpoint or transaction
// that produced them. HistoryView is the deliberate exception: it is a live reference to
// the Editor's replay log and cursor. Every view is storage-independent: it contains no
// parent or sibling ids, because those describe a position inside Model's hive rather
// than anything a front end can display or act on.
//
// `text` is a text element's words, read out of the character data under it. SVG keeps them
// in a child node rather than in an attribute, but a tree in which every word is a line of
// its own is unreadable, so the two arrive together and the words are printed inside the
// element that holds them.
// The data variants without their tree-storage fields. `Container` and `Text` own the
// first/last child links inside Model, so projecting only their SVG payload prevents
// those hive ids from becoming part of the view contract.
struct ContainerShape {
    std::variant<Container::Group, Container::Defs, Container::SVG, Container::Symbol, Container::Switch> specific;
};
struct TextShape {
    Text::Common common;
    std::variant<Text::Wrapper, Text::Span, Text::Path> specific;
};
using ElementData = std::variant<Shape, ContainerShape, TextShape>;

struct ElementShape {
    IdentityAttr identity_attr;
    PaintAttr paint_attr;
    ElementData data;
};
struct CharacterView {
    std::string content;
};
using NodeShape = std::variant<ElementShape, CharacterView>;

struct TreeRow {
    std::size_t depth;
    bool last;  // whether this node is the last child of its parent, which is what a tree drawing branches on
    Id node;
    ElementShape shape;
    std::string text;
    // The element's composed transform matrix; identity when it carries none.
    Transform transform = {};
};
struct TreeView { std::vector<TreeRow> rows; };
// Where a shown node hangs. The root has no parent, so this is absent there; every
// other node has one, and the front end may name it beside the node's own heading.
struct NodeParent {
    Id node;
    ElementShape shape;
};
struct NodeView {
    Id node;
    NodeShape shape;
    std::string text;
    std::optional<NodeParent> parent = std::nullopt;
    Transform transform = {};
};

// One of the two reads, or nothing: an outcome that is a change or a failure carries its
// sentence instead of a view.
using View = std::variant<std::monostate, TreeView, NodeView, HistoryView>;
