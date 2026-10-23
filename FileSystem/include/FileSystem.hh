#pragma once

#include <string>

#include "Document.hh"
#include "Intent.hh"
#include "Outcome.hh"

// FileSystem is the context-write path. Like Editor, it receives an already typed
// instruction and owns the lower-level work behind it: file bytes, XML/SVG syntax,
// remembered source paths, and context history baselines.
class FileSystem {
public:
    explicit FileSystem(Document& document);

    Outcome translate(const FileIntent& intent);

private:
    template<typename T>
    Outcome handle(const T& intent) {
        static_assert(false, "the FileSystem has no translation for this intent");
        return refused(Kind::Context, fault::InternalError{});
    }

    Document& document_;
    std::string source_path_;
};
