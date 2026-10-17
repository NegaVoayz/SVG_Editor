#pragma once
#include <concepts>
#include <string>
#include <variant>
#include <optional>

#include "BasicType.hh"
#include "Attr.hh"

struct Shape {
    struct Rect { LengthPercentage x, y, w, h; };
    struct Circle { LengthPercentage cx, cy, r; };
    struct Ellipse { LengthPercentage cx, cy, rx, ry; };
    struct Line { LengthPercentage x1, y1, x2, y2; };
    std::variant<Rect,Circle,Ellipse,Line> specific;
};

struct PlainText {
    std::string content;
    // Character data is a node of the tree like any other - a text element holds it - so it
    // says who holds it the same way an element does. See Node below.
    Id parent_link = no_id;
};

struct Text { 
    struct Common {
        std::optional<LengthPercentage> x, y, dx, dy;
    } common;

    ChildAttr child_attr;
    struct Wrapper {};
    struct Span {};
    struct Path { std::optional<LengthPercentage> startOffset; std::string href; };
    std::variant<Wrapper,Span,Path> specific;
};

struct Container {
    ChildAttr child_attr;

    struct Group {};
    struct Defs {};
    // The root element's own size, which is the canvas of the document: it belongs
    // to the <svg> element and not to the document beside it, because that is where
    // SVG writes it and what the serializer will write it back out of.
    struct SVG { std::optional<LengthPercentage> width, height; };
    struct Symbol {};
    struct Switch {};
    std::variant<Group, Defs, SVG, Symbol, Switch> specific;
};

struct SVGNode {
    IdentityAttr identity_attr;
    PaintAttr paint_attr;
    TransformAttr transform_attr;

    // The row in TreeStore's parent hive that says who holds this element. It is part of what
    // the node is, not where it is: a move writes the row's contents, never this handle, which
    // is what keeps a move from rewriting the payload it moves.
    Id parent_link = no_id;

    std::variant<Shape,Container,Text> data;
};

using Node = std::variant<SVGNode, PlainText>;

// Whether a payload type carries the handle of the row that says who holds it.
template<typename T>
concept Linked = requires(T candidate, Id link) {
    { candidate.parent_link = link } -> std::same_as<Id&>;
};

// A node is the whole record the arena stores: there is no wrapper around the variant, so
// every alternative of it has to say who holds it itself. Folding over the alternatives is
// how that is checked without listing them, and a new kind of node that forgets the link is
// a build error here.
template<typename... Alternatives>
consteval bool all_linked(std::variant<Alternatives...>*) {
    return (Linked<Alternatives> && ...);
}
static_assert(all_linked(static_cast<Node*>(nullptr)), "every alternative of Node needs a parent link");

// Whether a payload type can hold children: it is the types that have a child link, and
// nothing lists them. The link is the storage the store writes its row's handle into, so a
// type that has it really can hold children, and one that has not cannot. The answer is not a
// claim that can be wrong: a type saying it does without the member does not satisfy this, and
// neither does one whose member is not an Id.
template<typename T>
concept ChildHolder = requires(T candidate, Id link) {
    { candidate.child_attr.links = link } -> std::same_as<Id&>;
};

// A node that is an SVG element carries the variant of what element it is; character data is
// the other alternative of Node and carries nothing. Asking for `.data` tells the two apart
// without naming either.
template<typename T>
concept Element = requires(T candidate) { candidate.data; };
