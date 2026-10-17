#pragma once
#include <limits>
#include <variant>

#include "BasicType.hh"

// A link that points at nothing: no payload ever lives at this id, so a walk that
// reads it stops. It is the same value the hive calls npos, but Model's own name
// for it, because the public headers cannot see SwapDiffHive.
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
// link, not an attribute of the element, and it lives in TreeStore's link tables.
struct IdentityAttr { std::string id, class_; };
struct PaintAttr { std::optional<Paint> fill, stroke; std::optional<LengthPercentage> stroke_width; };
struct TransformAttr { std::optional<Transform> transform; };

// The row in TreeStore's child hive that holds this element's child chain: the last edge of
// the chain lives there, not in the payload, so adding or removing a child does not rewrite
// the element. Having this member is what can hold children - it is the storage the store
// writes the row's handle into, not a flag claiming the element has one. A payload is built
// with no_id here and the store fills it in when the element is added to a tree.
struct ChildAttr { Id links; };
