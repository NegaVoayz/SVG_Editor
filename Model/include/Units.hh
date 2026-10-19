#pragma once

#include <optional>

#include "BasicType.hh"

// What a length is worth, and what it may be added to. This is the whole of the unit
// table - the systems lengths come in, how far apart the units of one system are, what a
// sum of two values is allowed to be, and the sentence for one that is not.
//
// It lives in Model because a transform composes lengths as part of its matrix, and the
// Editor's own param arithmetic uses the same table rather than keeping a second one.
namespace Geometry {

// The same kind of value, in a unit the model can convert, or nothing. The sum is written
// in the unit the value already had, so an element keeps the units it was written with -
// except where one side is zero and the other brings its own unit.
std::optional<LengthPercentage> sum(LengthPercentage left, LengthPercentage right);

// One value, times the factor. A scale is a multiplication and no addition, so nothing
// here converts anything: a percentage stays a percentage and a length stays in its own
// unit, and no two systems can meet.
LengthPercentage scaled(LengthPercentage value, float factor);

}  // namespace Geometry
