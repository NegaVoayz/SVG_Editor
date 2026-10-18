#pragma once

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

#include "SvgDiagnostic.hh"

// The result of an operation that can fail for a reason the user has to be told about.
// Like the protocol's outcome, this is data rather than a sentence: the ui layer decides
// how a failed read or write is explained.
namespace model_status {

struct Ok {};
struct Opened {
    std::vector<AssignedId> assigned_ids;
};
struct NoDocument {};
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

}  // namespace model_status

using Status = std::variant<model_status::Ok, model_status::Opened, model_status::NoDocument,
                            model_status::MissingSourcePath, model_status::CannotReadFile,
                            model_status::CannotWriteFile, model_status::InvalidSvg>;

inline bool ok(const Status& status) {
    return std::holds_alternative<model_status::Ok>(status) || std::holds_alternative<model_status::Opened>(status);
}
