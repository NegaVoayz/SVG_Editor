#pragma once

#include <cstddef>
#include <string>

// What a read changed without refusing: the file is valid SVG for this model, but an
// element had no id, so the loader gave it one. The front end decides how to say this.
struct AssignedId {
    std::string id;
    std::string element;
};

enum class SvgProblem {
    MalformedXml,
    NoRootElement,
    RootIsNotSvg,
    UnsupportedElement,
    UnsupportedAttribute,
    InvalidAttributeValue,
    DuplicateId,
    UnexpectedText,
};

// A parse failure with the values a front end needs to explain it. `detail` carries a
// parser token or a short syntax reason, not a sentence written for the user.
struct SvgDiagnostic {
    SvgProblem problem = SvgProblem::MalformedXml;
    std::size_t line = 0;
    std::string element;
    std::string attribute;
    std::string value;
    std::string detail;
};
