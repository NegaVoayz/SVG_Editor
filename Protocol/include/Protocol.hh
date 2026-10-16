#pragma once

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

// --- the instructions ---

struct CreateRect {
    std::string id;
    LengthPercentage x, y, width, height;
    std::optional<Color> fill;
};
struct CreateGroup { std::string id; };
struct Delete { NodeRef node; };
struct Move { NodeRef node; LengthPercentage dx, dy; };
struct AddTo { NodeRef parent, node; };
struct Undo {};
struct Redo {};
struct ShowTree {};
struct Find { std::string svg_id; };
struct Open { std::string path; };
struct Save { std::string path; };

// One instruction, of exactly one of those types: the front end produces it and
// both the Planner and the Editor dispatch on it with std::visit, so every case is
// answered by a specialization rather than by a chain over verbs.
using Intent =
    std::variant<CreateRect, CreateGroup, Delete, Move, AddTo, Undo, Redo, ShowTree, Find, Open, Save>;

// The change instructions on their own: the Editor is typed to these, so a view or
// a context instruction reaching the write path is not a runtime branch it has to
// refuse, it is a type it does not accept.
using ChangeIntent = std::variant<CreateRect, CreateGroup, Delete, Move, AddTo, Undo, Redo>;

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

// What comes back: a message for the user, and which branch produced it.
struct Outcome {
    bool ok = true;
    Kind kind = Kind::Unknown;
    std::string message;
};
