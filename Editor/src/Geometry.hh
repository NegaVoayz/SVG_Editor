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

}  // namespace Geometry
