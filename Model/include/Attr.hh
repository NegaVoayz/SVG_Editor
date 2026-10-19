#pragma once
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>
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
struct Rotate {
    struct Center { LengthPercentage x, y; };
    float degrees;
    std::optional<Center> center = std::nullopt;
};
struct SkewX { float degrees; };
struct SkewY { float degrees; };
struct Matrix { float a, b, c, d, e, f; };
using Transform = std::variant<Translate, Scale, Rotate, SkewX, SkewY, Matrix>;

// One step of a transform list and the step before it. A list is stored as immutable
// links: an edit adds a record that points at the previous head, so the node only swaps
// one index and every older version of the node still names the chain it was written
// with. A chain is read from the head towards no_id, which is the order SVG writes its
// transform functions in.
struct TransformLink {
    Transform transform;
    Id previous = no_id;
};

struct TransformText {
    std::string operator()(const Translate& translate) const {
        return "translate(" + to_text(translate.x) + " " + to_text(translate.y) + ")";
    }
    std::string operator()(const Scale& scale) const {
        return "scale(" + number_text(scale.x) + " " + number_text(scale.y) + ")";
    }
    std::string operator()(const Rotate& rotate) const {
        std::string text = "rotate(" + number_text(rotate.degrees);
        if (rotate.center) {
            text += " " + to_text(rotate.center->x) + " " + to_text(rotate.center->y);
        }
        return text + ")";
    }
    std::string operator()(const SkewX& skew) const {
        return "skewX(" + number_text(skew.degrees) + ")";
    }
    std::string operator()(const SkewY& skew) const {
        return "skewY(" + number_text(skew.degrees) + ")";
    }
    std::string operator()(const Matrix& matrix) const {
        return "matrix(" + number_text(matrix.a) + " " + number_text(matrix.b) + " " + number_text(matrix.c) + " " +
               number_text(matrix.d) + " " + number_text(matrix.e) + " " + number_text(matrix.f) + ")";
    }
};

inline std::string to_text(const Transform& transform) { return std::visit(TransformText{}, transform); }

// The attribute the user reads and the serializer writes. The steps are already in
// document order, separated by the space SVG writes between transform functions.
inline std::string transform_list_text(std::span<const Transform> transforms) {
    std::string text;
    for (const Transform& transform : transforms) {
        if (!text.empty()) text += ' ';
        text += to_text(transform);
    }
    return text;
}

struct FiniteTransform {
    bool operator()(const Translate& translate) const {
        return is_finite(translate.x) && is_finite(translate.y);
    }
    bool operator()(const Scale& scale) const { return std::isfinite(scale.x) && std::isfinite(scale.y); }
    bool operator()(const Rotate& rotate) const {
        return std::isfinite(rotate.degrees) &&
               (!rotate.center || (is_finite(rotate.center->x) && is_finite(rotate.center->y)));
    }
    bool operator()(const SkewX& skew) const { return std::isfinite(skew.degrees); }
    bool operator()(const SkewY& skew) const { return std::isfinite(skew.degrees); }
    bool operator()(const Matrix& matrix) const {
        return std::isfinite(matrix.a) && std::isfinite(matrix.b) && std::isfinite(matrix.c) &&
               std::isfinite(matrix.d) && std::isfinite(matrix.e) && std::isfinite(matrix.f);
    }
};

inline bool is_finite(const Transform& transform) { return std::visit(FiniteTransform{}, transform); }

// What an element is called. Where it sits in the tree is not part of this: a parent is a
// link stored beside the node in the hive, not an SVG attribute.
struct IdentityAttr { std::string id, class_; };
struct PaintAttr { std::optional<Paint> fill, stroke; std::optional<LengthPercentage> stroke_width; };
// The head of this element's transform chain, or no_id when it has none. The link
// records live beside the tree, in the same storage that owns the tree, because the
// transform is part of the element and has to follow its undo history.
struct TransformAttr { Id head = no_id; };

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
