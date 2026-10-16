#pragma once

#include <string>

#include "SVGNode.hh"

// Moving a shape means moving its own coordinates, which is arithmetic on values
// the model owns: the syntax of those values is the front end's, and the meaning of
// the instruction is the Editor's.
namespace Geometry {

// Adds the offset to every coordinate of the shape, and answers false with a
// sentence when the two sides do not add up.
bool translate(Node& payload, LengthPercentage dx, LengthPercentage dy, std::string& problem);

// Multiplies every coordinate and size the node owns, and answers false with a
// sentence when there is nothing to multiply or the factor cannot be written. A
// shape's values are multiplied where they stand - each in the unit it was written
// in, so a scale never asks the unit table anything - and a node without values of
// its own carries a scale in its transform instead.
bool scale(Node& payload, float factor, std::string& problem);

}  // namespace Geometry
