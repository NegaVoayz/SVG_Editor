#pragma once
#include <limits>
#include <variant>

#include "BasicType.hh"
#include "Color.hh"

// A link that points at nothing: no node ever lives at this id, so a walk that
// reads it stops. It is one past the end of the virtual-id range, not a slot that
// something is reserved in.
inline constexpr Id no_id = std::numeric_limits<Id>::max();

// A transform, in the forms SVG writes it. The simple forms are kept as the forms
// they are, because they say exactly what they mean and stay exact under an edit;
// the matrix is what a transform becomes when a simple form can no longer hold what
// it has to carry.
struct Translate { LengthPercentage x, y; };
struct Scale { float x, y; };
struct Rotate { float degrees; };
struct Matrix { float a, b, c, d, e, f; };
using Transform = std::variant<Translate, Scale, Rotate, Matrix>;

// What an element is called. Where it sits in the tree is not part of this: a parent is a
// link stored beside the node in the hive, not an SVG attribute.
struct IdentityAttr { std::string id, class_; };
struct PaintAttr { std::optional<Paint> fill, stroke; std::optional<LengthPercentage> stroke_width; };
struct TransformAttr { std::optional<Transform> transform; };

// The two ends of an element's child list. A node does not keep a vector of children:
// each child keeps its own previous and next sibling, so placing, moving, or removing a
// child writes the links that change instead of rewriting a list. Having this member is
// what can hold children - it is the storage the tree writes, not a flag claiming the
// element has somewhere to put a child.
struct ChildAttr {
    Id first_child = no_id;
    Id last_child = no_id;
};
