#pragma once
#include <limits>
#include <string_view>
#include <variant>

#include "BasicType.hh"
#include "Color.hh"

// A link that points at nothing: no node ever lives at this id, so a walk that
// reads it stops. It is one past the end of the virtual-id range, not a slot that
// something is reserved in.
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

// What an element is called. Where it sits in the tree is not part of this: a parent is a
// link stored beside the node in the hive, not an SVG attribute.
struct IdentityAttr { std::string id, class_; };
struct PaintAttr { std::optional<Paint> fill, stroke; std::optional<LengthPercentage> stroke_width; };
struct TransformAttr { std::optional<Transform> transform; };

// SVG ids are XML names: they have no whitespace, start with a name-start character and
// continue with name characters. Non-ASCII bytes are accepted as name characters here;
// ASCII punctuation that XML excludes is not, so a quoted command cannot smuggle a space
// or a quote into an id.
inline bool svg_id_start(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value == '_' || value == ':' ||
           value >= 0x80;
}

inline bool svg_id_character(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return svg_id_start(character) || (value >= '0' && value <= '9') || value == '-' || value == '.';
}

inline bool valid_svg_id(std::string_view id) {
    if (id.empty() || !svg_id_start(id.front())) return false;
    for (const char character : id.substr(1)) {
        if (!svg_id_character(character)) return false;
    }
    return true;
}

// The root element is an <svg>, and `svg` is how a reference names the root. Keeping that
// word free means every id the document holds can also be typed: a node named `svg` would
// otherwise be shadowed by the root alias, with no way to reach it.
inline constexpr std::string_view root_alias = "svg";

inline bool reserved_svg_id(std::string_view id) { return id == root_alias; }

// The two ends of an element's child list. A node does not keep a vector of children:
// each child keeps its own previous and next sibling, so placing, moving, or removing a
// child writes the links that change instead of rewriting a list. Having this member is
// what can hold children - it is the storage the tree writes, not a flag claiming the
// element has somewhere to put a child.
struct ChildAttr {
    Id first_child = no_id;
    Id last_child = no_id;
};
