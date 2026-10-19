#pragma once

#include <cstddef>
#include <string>

// What a read changed without refusing. An element with no id was given one; an element
// that carried the reserved root alias as its id was renamed. `original_id` is empty in
// the first case and holds the replaced id in the second, so the front end can say which
// happened without inferring it from the values.
struct AssignedId {
    std::string id;
    std::string element;
    std::string original_id;
};

enum class SvgProblem {
    MalformedXml,
    NoRootElement,
    RootIsNotSvg,
    UnsupportedElement,
    UnsupportedAttribute,
    InvalidAttributeValue,
    DuplicateId,
    InvalidId,
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
