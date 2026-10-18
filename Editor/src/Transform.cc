// The transform algebra: what a translation and a scale do to a node that has no values of
// their own to land in, and how a simple form gives way to a matrix when it has to carry
// both.
#include "Transform.hh"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <variant>

#include "Units.hh"

namespace Geometry {

namespace {

// The matrix a transform stands for. Scale and matrix are exact; a rotation needs its
// cosine and sine, which is where a matrix stops being exact.
struct ToMatrix {
    Matrix operator()(const Translate&) const {
        // A translate stays a translate: move_transform extends it instead of asking.
        throw std::logic_error("Geometry: a translate is never promoted to a matrix");
    }
    Matrix operator()(const Scale& scale) const { return Matrix{scale.x, 0.0F, 0.0F, scale.y, 0.0F, 0.0F}; }
    Matrix operator()(const Rotate& rotate) const { return rotation_matrix(rotate.degrees); }
    Matrix operator()(const Matrix& matrix) const { return matrix; }
};

}  // namespace

Matrix rotation_matrix(float degrees) {
    constexpr float pi = 3.14159265358979323846F;
    const float radians = degrees * pi / 180.0F;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return Matrix{cosine, sine, -sine, cosine, 0.0F, 0.0F};
}

std::optional<float> user_units_of(LengthPercentage value) {
    if (!std::holds_alternative<Length>(value)) return std::nullopt;
    const Length& length = std::get<Length>(value);
    if (length.unit != Length::Unit::UserUnit) return std::nullopt;
    return length.value;
}

// A container has no coordinates to change, so a translation becomes its transform.
std::optional<Refusal> move_transform(SVGNode& element, LengthPercentage dx, LengthPercentage dy) {
    std::optional<Transform>& transform = element.transform_attr.transform;

    if (!transform) {
        transform = Translate{dx, dy};  // one translate says exactly what happened
        return std::nullopt;
    }
    if (std::holds_alternative<Translate>(*transform)) {
        Translate& translation = std::get<Translate>(*transform);
        const std::optional<LengthPercentage> x = sum(translation.x, dx);
        if (!x) return Refusal{fault::IncompatibleLengths{dx, translation.x}};
        const std::optional<LengthPercentage> y = sum(translation.y, dy);
        if (!y) return Refusal{fault::IncompatibleLengths{dy, translation.y}};
        translation.x = *x;
        translation.y = *y;
        return std::nullopt;
    }

    // Anything else becomes a matrix, because no single simple form holds a translation
    // next to a scale or a rotation. The translation lands in e and f, which puts it in
    // front of the transform the node already carried: the node moves in its parent's
    // coordinate system, which is what moving it means.
    const std::optional<float> offset_x = user_units_of(dx);
    const std::optional<float> offset_y = user_units_of(dy);
    if (!offset_x || !offset_y) {
        return Refusal{fault::MatrixNeedsUserUnitsForMove{dx}};
    }
    const Matrix existing = std::visit(ToMatrix{}, *transform);
    transform = Matrix{existing.a, existing.b, existing.c, existing.d, existing.e + *offset_x, existing.f + *offset_y};
    return std::nullopt;
}

std::optional<Refusal> scale_transform(SVGNode& element, float factor) {
    std::optional<Transform>& transform = element.transform_attr.transform;

    if (!transform) {
        transform = Scale{factor, factor};  // a scale stays a scale
        return std::nullopt;
    }
    if (std::holds_alternative<Scale>(*transform)) {
        Scale& scale = std::get<Scale>(*transform);
        scale.x *= factor;
        scale.y *= factor;
        return std::nullopt;
    }
    if (std::holds_alternative<Rotate>(*transform)) {
        const Matrix turned = rotation_matrix(std::get<Rotate>(*transform).degrees);
        transform = Matrix{turned.a * factor, turned.b * factor, turned.c * factor, turned.d * factor, 0.0F, 0.0F};
        return std::nullopt;
    }
    if (std::holds_alternative<Matrix>(*transform)) {
        Matrix& matrix = std::get<Matrix>(*transform);
        // the two translation columns stay where they are: this is a scale about the
        // node's own origin, not about wherever the matrix moves it to
        matrix.a *= factor;
        matrix.b *= factor;
        matrix.c *= factor;
        matrix.d *= factor;
        return std::nullopt;
    }

    // A translation has to become a matrix to take a scale behind it, and a matrix counts
    // in user units, so a translation written in a unit cannot be part of one.
    const Translate& translation = std::get<Translate>(*transform);
    const std::optional<float> x = user_units_of(translation.x);
    const std::optional<float> y = user_units_of(translation.y);
    if (!x || !y) {
        return Refusal{fault::MatrixNeedsUserUnitsForScale{translation.x, translation.y}};
    }
    transform = Matrix{factor, 0.0F, 0.0F, factor, *x, *y};
    return std::nullopt;
}

}  // namespace Geometry
