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

inline constexpr double transform_pi = 3.141592653589793238462643383279502884;

inline double radians_of(double degrees) { return degrees * (transform_pi / 180.0); }
inline double degrees_of(double radians_value) { return radians_value * (180.0 / transform_pi); }

inline bool transform_number_is(double value, double expected, double tolerance) {
    return std::fabs(value - expected) <= tolerance;
}

inline bool transform_length_is_zero(const LengthPercentage& value) {
    return numeric_value(value) == 0.0F;
}

inline bool transform_length_is_near_zero(const LengthPercentage& value, double tolerance) {
    return std::fabs(static_cast<double>(numeric_value(value))) <= tolerance;
}

inline bool is_finite(const Transform& transform) {
    return std::isfinite(transform.a) && std::isfinite(transform.b) && std::isfinite(transform.c) &&
           std::isfinite(transform.d) && is_finite(transform.e) && is_finite(transform.f);
}

inline bool is_identity(const Transform& transform, double tolerance = 1.0e-5) {
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

inline Transform scaled(double x, double y) {
    Transform transform;
    transform.a = static_cast<float>(x);
    transform.d = static_cast<float>(y);
    return transform;
}

inline Transform rotated(double degrees_value) {
    Transform transform;
    const double angle = radians_of(degrees_value);
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    transform.a = static_cast<float>(cosine);
    transform.b = static_cast<float>(sine);
    transform.c = static_cast<float>(-sine);
    transform.d = static_cast<float>(cosine);
    return transform;
}

inline Transform skewed_x(double degrees_value) {
    Transform transform;
    transform.c = static_cast<float>(std::tan(radians_of(degrees_value)));
    return transform;
}

inline Transform skewed_y(double degrees_value) {
    Transform transform;
    transform.b = static_cast<float>(std::tan(radians_of(degrees_value)));
    return transform;
}

inline Transform matrix_transform(double a, double b, double c, double d, double e, double f) {
    Transform transform;
    transform.a = static_cast<float>(a);
    transform.b = static_cast<float>(b);
    transform.c = static_cast<float>(c);
    transform.d = static_cast<float>(d);
    transform.e = Length{static_cast<float>(e), Length::Unit::UserUnit};
    transform.f = Length{static_cast<float>(f), Length::Unit::UserUnit};
    return transform;
}

// The matrix `outer` applied after `inner`: the SVG order of `outer inner`. The translation
// is a vector of lengths, so a unit sum that has no exact value makes the composition
// fail rather than approximate.
inline std::optional<Transform> compose(const Transform& outer, const Transform& inner) {
    Transform result;
    const double oa = outer.a;
    const double ob = outer.b;
    const double oc = outer.c;
    const double od = outer.d;
    const double ia = inner.a;
    const double ib = inner.b;
    const double ic = inner.c;
    const double id = inner.d;
    result.a = static_cast<float>(oa * ia + oc * ib);
    result.b = static_cast<float>(ob * ia + od * ib);
    result.c = static_cast<float>(oa * ic + oc * id);
    result.d = static_cast<float>(ob * ic + od * id);

    std::optional<LengthPercentage> x = Geometry::sum(Geometry::scaled(inner.e, oa),
                                                      Geometry::scaled(inner.f, oc));
    if (!x) return std::nullopt;
    x = Geometry::sum(*x, outer.e);
    if (!x) return std::nullopt;

    std::optional<LengthPercentage> y = Geometry::sum(Geometry::scaled(inner.e, ob),
                                                      Geometry::scaled(inner.f, od));
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
        compose(rotated(degrees_value), translated(Geometry::scaled(x, -1.0), Geometry::scaled(y, -1.0)));
    if (!centered) return std::nullopt;
    return compose(translated(x, y), *centered);
}

enum class SkewAxis { X, Y };

// The canonical read-side form: translate, rotate, scale, then one skew. Both a skewX and
// a skewY decomposition are exact. Prefer the one whose rendering is shorter; when both
// render at the same length, the smaller absolute skew is easier to read.
struct TransformParts {
    LengthPercentage x, y;
    float rotation = 0.0F;
    float scale_x = 1.0F;
    float scale_y = 1.0F;
    float skew = 0.0F;
    SkewAxis skew_axis = SkewAxis::X;
};

inline TransformParts decomposed_with_x(const Transform& transform) {
    TransformParts parts;
    parts.x = transform.e;
    parts.y = transform.f;
    parts.skew_axis = SkewAxis::X;

    const double a = transform.a;
    const double b = transform.b;
    const double c = transform.c;
    const double d = transform.d;
    const double scale_x = std::hypot(a, b);
    parts.scale_x = static_cast<float>(scale_x);
    if (scale_x > 0.0) {
        const double cosine = a / scale_x;
        const double sine = b / scale_x;
        const double determinant = a * d - b * c;
        parts.scale_y = static_cast<float>(determinant / scale_x);
        parts.rotation = static_cast<float>(degrees_of(std::atan2(b, a)));
        // Solve the skew from the column that is larger, so a rotation near 90 degrees does
        // not divide by a value that is only numerical noise.
        const double skew =
            std::fabs(a) >= std::fabs(b) ? (c + sine * static_cast<double>(parts.scale_y)) / a
                                        : (d - cosine * static_cast<double>(parts.scale_y)) / b;
        parts.skew = static_cast<float>(degrees_of(std::atan(skew)));
        return parts;
    }

    // The first column is zero: the matrix maps every x onto the same point. A rotation and
    // a y scale can still name the remaining column, and the skew is irrelevant there.
    parts.scale_y = static_cast<float>(std::hypot(c, d));
    parts.rotation = parts.scale_y > 0.0F ? static_cast<float>(degrees_of(std::atan2(-c, d))) : 0.0F;
    return parts;
}

inline TransformParts decomposed_with_y(const Transform& transform) {
    TransformParts parts;
    parts.x = transform.e;
    parts.y = transform.f;
    parts.skew_axis = SkewAxis::Y;

    const double a = transform.a;
    const double b = transform.b;
    const double c = transform.c;
    const double d = transform.d;
    const double scale_y = std::hypot(c, d);
    parts.scale_y = static_cast<float>(scale_y);
    if (scale_y > 0.0) {
        const double cosine = d / scale_y;
        const double sine = -c / scale_y;
        parts.rotation = static_cast<float>(degrees_of(std::atan2(-c, d)));
        parts.scale_x = static_cast<float>(a * cosine + b * sine);
        const double skew = (b * cosine - a * sine) / scale_y;
        parts.skew = static_cast<float>(degrees_of(std::atan(skew)));
        return parts;
    }

    // The second column is zero. The first column still names a rotation and an x scale,
    // and the skew is irrelevant because it multiplies that zero column.
    parts.scale_x = static_cast<float>(std::hypot(a, b));
    parts.rotation = parts.scale_x > 0.0F ? static_cast<float>(degrees_of(std::atan2(b, a))) : 0.0F;
    return parts;
}

inline std::string transform_parts_text(const TransformParts& parts, bool exact = false) {
    // A matrix is stored as float after its double-precision calculation. Values this close
    // to zero or one are display noise (2^-17 is about 7.6e-6), not an edit the user made.
    constexpr double value_tolerance = 1.0e-5;
    constexpr double angle_tolerance = 1.0e-3;

    std::string text;
    const auto number = [exact](float value) {
        return exact ? exact_number_text(value) : number_text(value);
    };
    auto append = [&text](std::string step) {
        if (!text.empty()) text += ' ';
        text += std::move(step);
    };
    if (!transform_length_is_near_zero(parts.x, value_tolerance) ||
        !transform_length_is_near_zero(parts.y, value_tolerance)) {
        // A zero is zero in every unit; printing the unit it happened to acquire from a
        // composed transform would only add noise.
        const std::string x =
            transform_length_is_near_zero(parts.x, value_tolerance)
                ? "0"
                : (exact ? svg_text(parts.x) : to_text(parts.x));
        const std::string y =
            transform_length_is_near_zero(parts.y, value_tolerance)
                ? "0"
                : (exact ? svg_text(parts.y) : to_text(parts.y));
        append("translate(" + x + " " + y + ")");
    }
    if (std::fabs(parts.rotation) > angle_tolerance) {
        append("rotate(" + number(parts.rotation) + ")");
    }
    if (!transform_number_is(parts.scale_x, 1.0F, value_tolerance) ||
        !transform_number_is(parts.scale_y, 1.0F, value_tolerance)) {
        append("scale(" + number(parts.scale_x) + " " + number(parts.scale_y) + ")");
    }
    if (std::fabs(parts.skew) > angle_tolerance) {
        append(std::string{parts.skew_axis == SkewAxis::Y ? "skewY(" : "skewX("} + number(parts.skew) + ")");
    }
    return text;
}

inline TransformParts decomposed(const Transform& transform) {
    const TransformParts x = decomposed_with_x(transform);
    const TransformParts y = decomposed_with_y(transform);
    const std::size_t x_size = transform_parts_text(x).size();
    const std::size_t y_size = transform_parts_text(y).size();
    if (x_size != y_size) return y_size < x_size ? y : x;
    return std::fabs(y.skew) < std::fabs(x.skew) ? y : x;
}

inline std::string transform_text(const Transform& transform) {
    return transform_parts_text(decomposed(transform));
}

inline std::string to_text(const Transform& transform) { return transform_text(transform); }

inline std::string svg_text(const Transform& transform) {
    return transform_parts_text(decomposed(transform), true);
}

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
