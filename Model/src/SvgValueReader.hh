#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "SVGNode.hh"

// The value spellings SVG attributes use. This is file syntax, not user-intent syntax:
// it lives in Model because a document reader must understand the bytes it opens.
namespace SvgValueReader {

std::optional<LengthPercentage> length(std::string_view text);
std::optional<Color> color(std::string_view text);
std::optional<Paint> paint(std::string_view text);
std::optional<std::vector<Transform>> transform(std::string_view text);
std::optional<Container::ViewBox> view_box(std::string_view text);
std::optional<Text::Anchor> text_anchor(std::string_view text);

}  // namespace SvgValueReader
