#pragma once
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "BasicType.hh"
#include "Color.hh"
#include "Units.hh"

// A link that points at nothing: no node ever lives at this id, so a walk that
// reads it stops. It is one past the end of the virtual-id range, not a slot that
// something is reserved in.
inline constexpr Id no_id = std::numeric_limits<Id>::max();

// A transform is one affine matrix:
//
//     [ a c e ]
//     [ b d f ]
//     [ 0 0 1 ]
//
// The four linear entries are plain numbers. The translation is carried in the document's
// length units, so composing a rotation with a millimetre offset does not silently turn the
// millimetre into a user unit. A transform list from a file or a command is composed into
// this one value; the user-facing form is the TRS+skew decomposition below.
struct Transform {
    float a = 1.0F;
    float b = 0.0F;
    float c = 0.0F;
    float d = 1.0F;
    LengthPercentage e = Length{0.0F, Length::Unit::UserUnit};
    LengthPercentage f = Length{0.0F, Length::Unit::UserUnit};
};

inline constexpr float transform_pi = 3.14159265358979323846F;

inline float radians_of(float degrees) { return degrees * (transform_pi / 180.0F); }
inline float degrees_of(float radians_value) { return radians_value * (180.0F / transform_pi); }

inline bool transform_number_is(float value, float expected, float tolerance) {
    return std::fabs(value - expected) <= tolerance;
}

inline bool transform_length_is_zero(const LengthPercentage& value) {
    return numeric_value(value) == 0.0F;
}

inline bool is_finite(const Transform& transform) {
    return std::isfinite(transform.a) && std::isfinite(transform.b) && std::isfinite(transform.c) &&
           std::isfinite(transform.d) && is_finite(transform.e) && is_finite(transform.f);
}

inline bool is_identity(const Transform& transform, float tolerance = 1.0e-6F) {
    return transform_number_is(transform.a, 1.0F, tolerance) &&
           transform_number_is(transform.b, 0.0F, tolerance) &&
           transform_number_is(transform.c, 0.0F, tolerance) &&
           transform_number_is(transform.d, 1.0F, tolerance) &&
           std::fabs(numeric_value(transform.e)) <= tolerance &&
           std::fabs(numeric_value(transform.f)) <= tolerance;
}

inline Transform translated(LengthPercentage x, LengthPercentage y) {
    Transform transform;
    transform.e = std::move(x);
    transform.f = std::move(y);
    return transform;
}

inline Transform scaled(float x, float y) {
    Transform transform;
    transform.a = x;
    transform.d = y;
    return transform;
}

inline Transform rotated(float degrees_value) {
    Transform transform;
    const float angle = radians_of(degrees_value);
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    transform.a = cosine;
    transform.b = sine;
    transform.c = -sine;
    transform.d = cosine;
    return transform;
}

inline Transform skewed_x(float degrees_value) {
    Transform transform;
    transform.c = std::tan(radians_of(degrees_value));
    return transform;
}

inline Transform skewed_y(float degrees_value) {
    Transform transform;
    transform.b = std::tan(radians_of(degrees_value));
    return transform;
}

inline Transform matrix_transform(float a, float b, float c, float d, float e, float f) {
    Transform transform;
    transform.a = a;
    transform.b = b;
    transform.c = c;
    transform.d = d;
    transform.e = Length{e, Length::Unit::UserUnit};
    transform.f = Length{f, Length::Unit::UserUnit};
    return transform;
}

// The matrix `outer` applied after `inner`: the SVG order of `outer inner`. The translation
// is a vector of lengths, so a unit sum that has no exact value makes the composition
// fail rather than approximate.
inline std::optional<Transform> compose(const Transform& outer, const Transform& inner) {
    Transform result;
    result.a = outer.a * inner.a + outer.c * inner.b;
    result.b = outer.b * inner.a + outer.d * inner.b;
    result.c = outer.a * inner.c + outer.c * inner.d;
    result.d = outer.b * inner.c + outer.d * inner.d;

    std::optional<LengthPercentage> x = Geometry::sum(Geometry::scaled(inner.e, outer.a),
                                                      Geometry::scaled(inner.f, outer.c));
    if (!x) return std::nullopt;
    x = Geometry::sum(*x, outer.e);
    if (!x) return std::nullopt;

    std::optional<LengthPercentage> y = Geometry::sum(Geometry::scaled(inner.e, outer.b),
                                                      Geometry::scaled(inner.f, outer.d));
    if (!y) return std::nullopt;
    y = Geometry::sum(*y, outer.f);
    if (!y) return std::nullopt;

    result.e = *x;
    result.f = *y;
    if (!is_finite(result)) return std::nullopt;
    return result;
}

inline std::optional<Transform> rotation_about(float degrees_value, LengthPercentage x, LengthPercentage y) {
    const std::optional<Transform> centered =
        compose(rotated(degrees_value), translated(Geometry::scaled(x, -1.0F), Geometry::scaled(y, -1.0F)));
    if (!centered) return std::nullopt;
    return compose(translated(x, y), *centered);
}

// The canonical read-side form: translate, rotate, scale, then skewX. It is exact for every
// finite matrix, including a mirror (a negative y scale) and a singular one (a zero scale).
struct TransformParts {
    LengthPercentage x, y;
    float rotation = 0.0F;
    float scale_x = 1.0F;
    float scale_y = 1.0F;
    float skew_x = 0.0F;
};

inline TransformParts decomposed(const Transform& transform) {
    TransformParts parts;
    parts.x = transform.e;
    parts.y = transform.f;

    const float scale_x = std::hypot(transform.a, transform.b);
    parts.scale_x = scale_x;
    if (scale_x > 0.0F) {
        const float cosine = transform.a / scale_x;
        const float sine = transform.b / scale_x;
        const float determinant = transform.a * transform.d - transform.b * transform.c;
        parts.scale_y = determinant / scale_x;
        parts.rotation = degrees_of(std::atan2(transform.b, transform.a));
        // Solve the skew from the column that is larger, so a rotation near 90 degrees does
        // not divide by a value that is only numerical noise.
        const float skew = std::fabs(transform.a) >= std::fabs(transform.b)
                               ? (transform.c + sine * parts.scale_y) / transform.a
                               : (transform.d - cosine * parts.scale_y) / transform.b;
        parts.skew_x = degrees_of(std::atan(skew));
        return parts;
    }

    // The first column is zero: the matrix maps every x onto the same point. A rotation and
    // a y scale can still name the remaining column, and the skew is irrelevant there.
    parts.scale_y = std::hypot(transform.c, transform.d);
    parts.rotation = parts.scale_y > 0.0F ? degrees_of(std::atan2(-transform.c, transform.d)) : 0.0F;
    return parts;
}

inline std::string transform_text(const Transform& transform) {
    constexpr float value_tolerance = 1.0e-6F;
    constexpr float angle_tolerance = 1.0e-5F;
    const TransformParts parts = decomposed(transform);

    std::string text;
    auto append = [&text](std::string step) {
        if (!text.empty()) text += ' ';
        text += std::move(step);
    };
    if (!transform_length_is_zero(parts.x) || !transform_length_is_zero(parts.y)) {
        // A zero is zero in every unit; printing the unit it happened to acquire from a
        // composed transform would only add noise.
        const std::string x = transform_length_is_zero(parts.x) ? "0" : to_text(parts.x);
        const std::string y = transform_length_is_zero(parts.y) ? "0" : to_text(parts.y);
        append("translate(" + x + " " + y + ")");
    }
    if (std::fabs(parts.rotation) > angle_tolerance) {
        append("rotate(" + number_text(parts.rotation) + ")");
    }
    if (!transform_number_is(parts.scale_x, 1.0F, value_tolerance) ||
        !transform_number_is(parts.scale_y, 1.0F, value_tolerance)) {
        append("scale(" + number_text(parts.scale_x) + " " + number_text(parts.scale_y) + ")");
    }
    if (std::fabs(parts.skew_x) > angle_tolerance) {
        append("skewX(" + number_text(parts.skew_x) + ")");
    }
    return text;
}

inline std::string to_text(const Transform& transform) { return transform_text(transform); }

// What an element is called. Where it sits in the tree is not part of this: a parent is a
// link stored beside the node in the hive, not an SVG attribute.
struct IdentityAttr { std::string id, class_; };
struct PaintAttr { std::optional<Paint> fill, stroke; std::optional<LengthPercentage> stroke_width; };
// The element's transform. The identity is the absence of a transform attribute, so a
// node version carries everything an undo snapshot needs.
struct TransformAttr { Transform transform{}; };

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
