#pragma once
#include <concepts>
#include <string>
#include <string_view>
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

    // Character data is a node of the tree like any other - a text element holds it - so
    // it carries its own place in the tree. The link is the node id, not a row in another
    // hive: one Node is the whole record the MVCC hive stores.
    Id parent_id = no_id;
    Id previous_sibling = no_id;
    Id next_sibling = no_id;
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

    // Where this node sits. The ids are node ids in the same hive, and no_id is the root's
    // parent and the end of either sibling list. A move rewrites this small part of the
    // node record; the MVCC hive keeps the old version for snapshots.
    Id parent_id = no_id;
    Id previous_sibling = no_id;
    Id next_sibling = no_id;

    std::variant<Shape,Container,Text> data;
};

using Node = std::variant<SVGNode, PlainText>;

struct ShapeElementName {
    std::string_view operator()(const Shape::Rect&) const { return "rect"; }
    std::string_view operator()(const Shape::Circle&) const { return "circle"; }
    std::string_view operator()(const Shape::Ellipse&) const { return "ellipse"; }
    std::string_view operator()(const Shape::Line&) const { return "line"; }
};

struct ContainerElementName {
    std::string_view operator()(const Container::SVG&) const { return "svg"; }
    std::string_view operator()(const Container::Group&) const { return "g"; }
    std::string_view operator()(const Container::Defs&) const { return "defs"; }
    std::string_view operator()(const Container::Symbol&) const { return "symbol"; }
    std::string_view operator()(const Container::Switch&) const { return "switch"; }
};

struct ElementName {
    std::string_view operator()(const Shape& shape) const { return std::visit(ShapeElementName{}, shape.specific); }
    std::string_view operator()(const Container& container) const {
        return std::visit(ContainerElementName{}, container.specific);
    }
    std::string_view operator()(const Text&) const { return "text"; }
};

// The SVG element name a payload is written with. Character data has no element of its
// own, so it has no answer here; asking about one is not a question a caller has.
inline std::string_view element_name(const SVGNode& element) {
    return std::visit(ElementName{}, element.data);
}

// The SVG element that can be a parent of shapes and other containers. A text also has
// child storage, for its character data, but it is not a container a user can put an
// element inside.
inline bool is_container(const Node& node) {
    if (!std::holds_alternative<SVGNode>(node)) return false;
    return std::holds_alternative<Container>(std::get<SVGNode>(node).data);
}

// Whether a payload type carries its place in the tree.
template<typename T>
concept Linked = requires(T candidate, Id link) {
    { candidate.parent_id = link } -> std::same_as<Id&>;
    { candidate.previous_sibling = link } -> std::same_as<Id&>;
    { candidate.next_sibling = link } -> std::same_as<Id&>;
};

// A node is the whole record the hive stores: there is no wrapper around the variant, so
// every alternative of it has to carry its own place. Folding over the alternatives is how
// that is checked without listing them, and a new kind of node that forgets a link is a
// build error here.
template<typename... Alternatives>
consteval bool all_linked(std::variant<Alternatives...>*) {
    return (Linked<Alternatives> && ...);
}
static_assert(all_linked(static_cast<Node*>(nullptr)), "every alternative of Node needs its tree links");

// Whether a payload type can hold children: it is the types that have the first/last child
// storage, and nothing lists them. The answer is not a claim that can be wrong: a type
// saying it does without the members does not satisfy this, and neither does one whose
// members are not Ids.
template<typename T>
concept ChildHolder = requires(T candidate, Id id) {
    { candidate.child_attr.first_child = id } -> std::same_as<Id&>;
    { candidate.child_attr.last_child = id } -> std::same_as<Id&>;
};

// A node that is an SVG element carries the variant of what element it is; character data is
// the other alternative of Node and carries nothing. Asking for `.data` tells the two apart
// without naming either.
template<typename T>
concept Element = requires(T candidate) { candidate.data; };
