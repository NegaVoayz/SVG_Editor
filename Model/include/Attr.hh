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

struct IdentityAttr { std::string id, class_; Id parent_id; };
struct PaintAttr { std::optional<Paint> fill, stroke; std::optional<LengthPercentage> stroke_width; };
struct TransformAttr { std::optional<Transform> transform; };
struct ChildAttr { Id last_child_edge; };
