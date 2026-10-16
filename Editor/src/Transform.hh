#pragma once

#include <optional>
#include <string>

#include "SVGNode.hh"

// What a node does with a translation or a scale when it has no values of its own to put
// them into: a container begins with a translate or a scale, and extends what it already
// carries - up to the point where no simple form holds both, and a matrix takes over.
//
// Nothing here knows how big a user unit is, and that is what decides the refusals: a
// matrix counts in user units, so anything that has to become a matrix to carry a value in
// some other unit is refused rather than approximated.
namespace Geometry {

// The matrix a rotation stands for: its cosine and sine, which is where a matrix stops
// being exact.
Matrix rotation_matrix(float degrees);

// A translation a matrix can hold: a matrix counts in user units, so an offset in any other
// unit, or a percentage, cannot be written into one.
std::optional<float> user_units_of(LengthPercentage value);

// A translation written into the node's transform.
bool move_transform(SVGNode& element, LengthPercentage dx, LengthPercentage dy, std::string& problem);

// A scale written into the node's transform, behind whatever the node already carries.
bool scale_transform(SVGNode& element, float factor, std::string& problem);

}  // namespace Geometry
