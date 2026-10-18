#pragma once

#include <optional>

#include "Refusal.hh"
#include "SVGNode.hh"

// Moving a shape means moving its own coordinates, which is arithmetic on values
// the model owns: the syntax of those values is the front end's, and the meaning of
// the instruction is the Editor's.
namespace Geometry {

// Adds the offset to every coordinate of the shape, or answers why the two sides do not
// add up. A refusal is data; the words for it belong to the front end.
std::optional<Refusal> translate(Node& payload, LengthPercentage dx, LengthPercentage dy);

// Multiplies every coordinate and size the node owns, or answers why it cannot. A shape's
// values are multiplied where they stand - each in the unit it was written in, so a scale
// never asks the unit table anything - and a node without values of its own carries a
// scale in its transform instead.
std::optional<Refusal> scale(Node& payload, float factor);

}  // namespace Geometry
