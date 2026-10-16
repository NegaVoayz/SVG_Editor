#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "SVGNode.hh"

// What a length is worth, and what it may be added to. This is the whole of the unit
// table - the systems lengths come in, how far apart the units of one system are, what a
// sum of two values is allowed to be, and the sentence for one that is not - and it is a
// source of its own because it is the one part of the arithmetic that is about the values
// rather than about the node they sit in.
namespace Geometry {

// The same kind of value, in a unit the model can convert, or nothing. The sum is written
// in the unit the value already had, so an element keeps the units it was written with -
// except where one side is zero and the other brings its own unit.
std::optional<LengthPercentage> sum(LengthPercentage left, LengthPercentage right);

// The sentence for an addition that has no sum: which two things were added, and why they
// do not add up. It names the system on each side, because "1in + 1cm" is the one a user
// is most likely to have written by mistake and the two numbers do not show it.
std::string no_sum(std::string_view offset, LengthPercentage offset_value, std::string_view value,
                   LengthPercentage value_value);

// One value, times the factor. A scale is a multiplication and no addition, so nothing
// here converts anything: a percentage stays a percentage and a length stays in its own
// unit, and no two systems can meet.
LengthPercentage scaled(LengthPercentage value, float factor);

}  // namespace Geometry
