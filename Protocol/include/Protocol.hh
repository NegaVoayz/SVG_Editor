#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The vocabulary the front ends speak: one structured instruction per thing a user
// can ask for, with model values in it rather than text. "create rect box 0 0 10
// 10 #ff0000" is a CreateRect by the time it leaves the ui layer, so nothing above
// that parses a string, and nothing below it needs to know how the instruction was
// written.

#include "Document.hh"
#include "SVGNode.hh"

// What a template that is meant to be specialized says when a case is missing: the
// primary template fails the build, so an intent with no handler is a compile
// error rather than a silent nothing.
template<typename T>
inline constexpr bool always_false = false;

// How an instruction names a node: the root, a handle ("#3"), or an SVG id. Only
// the Editor turns one of these into a node in the document.
struct RootRef {};
struct HandleRef { Id id; };
struct SvgIdRef { std::string name; };
using NodeRef = std::variant<RootRef, HandleRef, SvgIdRef>;

// The answer to what a reference names, or nothing when nothing is called that.
// Asking is the same question in both directions of the pipeline - the Editor writes
// to the node a reference names and the Planner reads it - so the answer is written
// once, next to the vocabulary that defines what a reference is. What to do about
// "nothing" is not: the Editor refuses the edit, and the Planner reports the miss.
std::optional<Id> look_up(const Document& document, const NodeRef& reference);

// How a reference reads in a sentence back to the user: the root, a handle, or the
// quoted name, which is what the user wrote either way.
std::string describe(const NodeRef& reference);

// --- the instructions ---

// Every create names the element it makes and where to put it: `parent` is where the
// command's `in <gid>` points, and the root when it said nothing, so a fresh element
// is never a node that nothing can reach.
struct CreateRect {
    std::string id;
    LengthPercentage x, y, width, height;
    std::optional<Color> fill;
    NodeRef parent = RootRef{};
};
struct CreateCircle {
    std::string id;
    LengthPercentage cx, cy, r;
    std::optional<Color> fill;
    NodeRef parent = RootRef{};
};
struct CreateEllipse {
    std::string id;
    LengthPercentage cx, cy, rx, ry;
    std::optional<Color> fill;
    NodeRef parent = RootRef{};
};
// A line has no inside to fill, so what it may carry is a stroke.
struct CreateLine {
    std::string id;
    LengthPercentage x1, y1, x2, y2;
    std::optional<Color> stroke;
    NodeRef parent = RootRef{};
};
// A text element and the character data inside it are two nodes, which is why the
// content travels here as its own string and becomes a child: a <text> holds text
// the way a group holds elements.
struct CreateText {
    std::string id;
    LengthPercentage x, y;
    std::string content;
    NodeRef parent = RootRef{};
};
struct CreateGroup {
    std::string id;
    NodeRef parent = RootRef{};
};
struct Delete { NodeRef node; };
struct Move { NodeRef node; LengthPercentage dx, dy; };
struct ScaleBy { NodeRef node; float factor; };
// A group comes into being where the first node it takes used to stand, and takes
// the rest of them in the order they were written.
struct Group { std::string id; std::vector<NodeRef> nodes; };
struct Ungroup { NodeRef node; };
struct AddTo { NodeRef parent, node; };
// The child chain is the z-order, first child in front.
struct Front { NodeRef node; };
struct Back { NodeRef node; };
struct Undo {};
struct Redo {};
struct SetCanvas { std::optional<LengthPercentage> width, height; };
// Back to a fresh document: an empty tree, the canvas the instruction gave (default
// 800x600 for whichever side it left out), no history and no file behind it.
struct Init { std::optional<LengthPercentage> width, height; };
struct ShowTree { NodeRef node = RootRef{}; };
struct Show { NodeRef node; };
struct Read { std::string path; };
// No path: write where the document was read from, which is the Model's to remember.
struct Save { std::optional<std::string> path; };

// One instruction, of exactly one of those types: the front end produces it and
// both the Planner and the Editor dispatch on it with std::visit, so every case is
// answered by a specialization rather than by a chain over verbs.
using Intent = std::variant<CreateRect, CreateCircle, CreateEllipse, CreateLine, CreateText, CreateGroup, Delete,
                            Move, ScaleBy, Group, Ungroup, AddTo, Front, Back, Undo, Redo, SetCanvas, Init, ShowTree,
                            Show, Read, Save>;

// The change instructions on their own: the Editor is typed to these, so a view or
// a context instruction reaching the write path is not a runtime branch it has to
// refuse, it is a type it does not accept.
using ChangeIntent = std::variant<CreateRect, CreateCircle, CreateEllipse, CreateLine, CreateText, CreateGroup,
                                  Delete, Move, ScaleBy, Group, Ungroup, AddTo, Front, Back, SetCanvas, Undo, Redo>;

// What the Planner decided a request is.
enum class Kind {
    View,     // read the document and report
    Change,   // edit the document, through the Editor
    Context,  // change what is being edited, through Model/FileSystem
    Unknown,  // nothing produced this outcome's branch, or it failed before one
};

inline std::string_view kind_name(Kind kind) {
    switch (kind) {
        case Kind::View: return "view";
        case Kind::Change: return "change";
        case Kind::Context: return "context";
        case Kind::Unknown: return "unknown";
    }
    return "unknown";
}

// What a view hands back: the document as data rather than as a sentence. The ui
// layer is what turns it into text, because text is its business in both directions
// of the boundary - it parses what a user wrote, and it writes what a user reads.
//
// The payloads are copies. A view is a snapshot of the tree as it stands, and a
// payload moves between slots as soon as anything is edited, so nothing here could be
// a reference into the arena.
//
// `text` is a text element's words, read out of the character data under it. SVG
// keeps them in a child node rather than in an attribute, but a tree in which every
// word is a line of its own is unreadable, so the two arrive together and the words
// are printed inside the element that holds them.
struct TreeRow {
    std::size_t depth;
    bool last;   // whether this node is the last child of its parent, which is what a tree drawing branches on
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

// One of the two reads, or nothing: an outcome that is a change or a failure carries
// its sentence instead of a view.
using View = std::variant<std::monostate, TreeView, NodeView>;

// What comes back: whether it worked, which branch produced it, a sentence about what
// happened, and what a view read.
struct Outcome {
    bool ok = true;
    Kind kind = Kind::Unknown;
    std::string message;
    View view{};
};

// --- what a reference names ---

inline std::optional<Id> look_up(const Document& document, const NodeRef& reference) {
    if (std::holds_alternative<RootRef>(reference)) return document.root();
    if (std::holds_alternative<HandleRef>(reference)) {
        const Id id = std::get<HandleRef>(reference).id;
        if (!document.node(id)) return std::nullopt;
        return id;
    }
    return document.find(std::get<SvgIdRef>(reference).name);
}

inline std::string describe(const NodeRef& reference) {
    if (std::holds_alternative<RootRef>(reference)) return "the root";
    if (std::holds_alternative<HandleRef>(reference)) return "#" + std::to_string(std::get<HandleRef>(reference).id);
    return "\"" + std::get<SvgIdRef>(reference).name + "\"";
}
