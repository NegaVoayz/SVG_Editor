#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "SVGNode.hh"

// The values SVG writes, read from the text that spells them. It is the front
// end's business: Model deals in LengthPercentage and Color and never sees the
// text they came from.
namespace SvgValue {

// "10" (a user unit), "10px", "1.5em", "50%". A unit SVG knows but this does not
// spell out reads as nothing.
std::optional<LengthPercentage> parse_length(std::string_view text);

// "none", "currentColor", a CSS named or system colour, "#rgb", "#rrggbb" and
// "rgb(r,g,b)".
std::optional<Color> parse_color(std::string_view text);

// A plain number, with no unit and no percentage: what a scale factor is written as.
std::optional<float> parse_number(std::string_view text);

// One or more of "translate(x [y])", "scale(x [y])", "rotate(deg [cx cy])",
// "skewX(deg)", "skewY(deg)" and "matrix(a b c d e f)", composed into the one matrix the
// model stores. The translate and rotate-centre forms take lengths, the others plain
// numbers; commas or spaces separate values and functions. An empty value is the identity.
std::optional<Transform> parse_transform(std::string_view text);

// "min-x min-y width height", four plain numbers. A viewBox has no units and cannot
// have a negative width or height.
std::optional<Container::ViewBox> parse_view_box(std::string_view text);

// "start", "middle" or "end".
std::optional<Text::Anchor> parse_text_anchor(std::string_view text);

// Coordinate pairs separated by commas or whitespace. Each coordinate may use the same
// length spelling as a geometry attribute.
std::optional<std::vector<Shape::Point>> parse_points(std::string_view text);

}  // namespace SvgValue
