#pragma once

#include <cstddef>
#include <string>
#include <variant>

#include "NodeRef.hh"
#include "SVGNode.hh"
#include "SvgDiagnostic.hh"

// Why an instruction could not be carried out, as data. A refusal carries the handles and
// values a front end needs to say what went wrong; it never carries the sentence itself.
// That keeps the wording in the ui layer, where it can differ between a terminal and a
// graphical front end without either one having to parse the other's prose.
namespace fault {

struct NoDocument {};

struct MissingShape {
    NodeRef node;
};

struct MissingContainer {
    NodeRef node;
};

struct NotContainer {
    NodeRef node;
};

struct DuplicateId {
    std::string id;
};

struct InvalidId {
    std::string id;
};

struct DuplicateGroupMember {
    NodeRef node;
};

struct UnknownAttribute {
    std::string name;
};

struct NegativeDimension {
    std::string attribute;
};

struct NonPositiveCanvas {
    std::string attribute;
};

struct NonFiniteValue {
    std::string name;
};

struct CannotUngroupNonContainer {
    NodeRef node;
};

struct CannotGroupIntoOwnDescendant {
    NodeRef node;
};

struct CannotGroupTopLevelContainer {
    std::string name;
};

struct CannotUngroupTopLevelContainer {
    std::string name;
};

struct ScaleFactorNotPositive {};

struct RootCannotBeErased {};

struct RootHasNoSiblings {};

struct EmptyGroup {};

struct NothingToUndo {};

struct UndoAfterIo {};

struct NothingToRedo {};

struct NoRootElement {};

struct RootIsNotContainer {};

struct RootIsNotSvg {};

struct MissingSourcePath {};

struct CannotReadFile {
    std::string path;
};

struct CannotWriteFile {
    std::string path;
};

struct InvalidSvg {
    SvgDiagnostic diagnostic;
};

// Two values that have no exact sum: a percentage with a length, or lengths from the
// metric and imperial systems. The ui layer knows how to explain the difference.
struct IncompatibleLengths {
    LengthPercentage offset;
    LengthPercentage value;
};

struct MatrixNeedsUserUnitsForMove {
    LengthPercentage offset;
};

struct MatrixNeedsUserUnitsForScale {
    LengthPercentage x;
    LengthPercentage y;
};

struct TextCannotMove {};

struct TextCannotScale {};

// A lower layer refused something the Editor should have checked, or a Model invariant
// failed. It is still a refusal, but it has no user-facing detail to expose.
struct InternalError {};

}  // namespace fault

using Refusal = std::variant<fault::NoDocument, fault::MissingShape, fault::MissingContainer, fault::NotContainer,
                             fault::DuplicateId, fault::InvalidId, fault::DuplicateGroupMember, fault::UnknownAttribute,
                             fault::NegativeDimension, fault::NonPositiveCanvas, fault::NonFiniteValue,
                             fault::CannotUngroupNonContainer,
                             fault::CannotGroupIntoOwnDescendant, fault::CannotGroupTopLevelContainer,
                             fault::CannotUngroupTopLevelContainer, fault::ScaleFactorNotPositive,
                             fault::RootCannotBeErased, fault::RootHasNoSiblings, fault::EmptyGroup,
                             fault::NothingToUndo, fault::UndoAfterIo, fault::NothingToRedo, fault::NoRootElement,
                             fault::RootIsNotContainer, fault::RootIsNotSvg, fault::MissingSourcePath,
                             fault::CannotReadFile, fault::CannotWriteFile, fault::InvalidSvg,
                             fault::IncompatibleLengths,
                             fault::MatrixNeedsUserUnitsForMove, fault::MatrixNeedsUserUnitsForScale,
                             fault::TextCannotMove, fault::TextCannotScale, fault::InternalError>;
