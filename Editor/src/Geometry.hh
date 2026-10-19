#pragma once

#include <optional>

#include "Refusal.hh"
#include "SVGNode.hh"

// Moving a node's own params means moving values the model owns: a shape's coordinates,
// or a text's position and font metrics. The syntax of those values is the front end's,
// and the meaning of the instruction is the Editor's.
namespace Geometry {

// Adds the offset to the node's own coordinates, or answers why the two sides do not add
// up. A container has no direct params; a text's x and y default to zero before the
// offset is added. A refusal is data; the words for it belong to the front end.
std::optional<Refusal> translate_params(Node& payload, LengthPercentage dx, LengthPercentage dy);

// Multiplies every coordinate and size the node owns, or answers why it cannot. A shape's
// values and a text's position, offsets and font size are multiplied where they stand -
// each in the unit it was written in, so a scale never asks the unit table anything.
std::optional<Refusal> scale_params(Node& payload, float factor);

}  // namespace Geometry
