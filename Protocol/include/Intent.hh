#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "NodeRef.hh"
#include "SVGNode.hh"

// The vocabulary a front end speaks: one structured instruction per thing a user can ask
// for, with model values in it rather than text. "create rect box 0 0 10 10 #ff0000" is a
// CreateRect by the time it leaves the ui layer, so nothing above that parses a string, and
// nothing below it needs to know how an instruction was written.

// What a template that is meant to be specialized says when a case is missing: the primary
// template fails the build, so an intent with no handler is a compile error rather than a
// silent nothing. Every layer that answers an instruction is built this way, which is why
// it lives next to the instructions themselves.
template<typename T>
inline constexpr bool always_false = false;

// --- the instructions ---

// Every create names the element it makes and where to put it: `parent` is where the
// command's `in <gid>` points, and the root when it said nothing, so a fresh element is
// never a node that nothing can reach.
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
// A text element and the character data inside it are two nodes, which is why the content
// travels here as its own string and becomes a child: a <text> holds text the way a group
// holds elements.
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
// A group comes into being where the first node it takes used to stand, and takes the rest
// of them in the order they were written.
struct Group { std::string id; std::vector<NodeRef> nodes; };
struct Ungroup { NodeRef node; };
struct AddTo { NodeRef parent, node; };
// The child chain is the z-order, first child in front.
struct Front { NodeRef node; };
struct Back { NodeRef node; };
struct Undo {};
struct Redo {};
struct SetCanvas { std::optional<LengthPercentage> width, height; };
// Back to a fresh document: an empty tree, the canvas the instruction gave (default 800x600
// for whichever side it left out), no history and no file behind it.
struct Init { std::optional<LengthPercentage> width, height; };
struct ShowTree { NodeRef node = RootRef{}; };
struct Show { NodeRef node; };
struct Read { std::string path; };
// No path: write where the document was read from, which is the Model's to remember.
struct Save { std::optional<std::string> path; };

// One instruction, of exactly one of those types: the front end produces it and both the
// Planner and the Editor dispatch on it with std::visit, so every case is answered by a
// specialization rather than by a chain over verbs.
using Intent = std::variant<CreateRect, CreateCircle, CreateEllipse, CreateLine, CreateText, CreateGroup, Delete,
                            Move, ScaleBy, Group, Ungroup, AddTo, Front, Back, Undo, Redo, SetCanvas, Init, ShowTree,
                            Show, Read, Save>;

// The change instructions on their own: the Editor is typed to these, so a view or a
// context instruction reaching the write path is not a runtime branch it has to refuse, it
// is a type it does not accept.
using ChangeIntent = std::variant<CreateRect, CreateCircle, CreateEllipse, CreateLine, CreateText, CreateGroup,
                                  Delete, Move, ScaleBy, Group, Ungroup, AddTo, Front, Back, SetCanvas, Undo, Redo>;
