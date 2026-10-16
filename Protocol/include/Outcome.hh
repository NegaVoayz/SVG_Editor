#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "View.hh"

// What comes back out of the pipeline: whether it worked, which branch produced it, a
// sentence about what happened, and what a read found when what was asked for was a read.

// What the Planner decided a request is.
enum class Kind {
    View,     // read the document and report
    Change,   // edit the document, through the Editor
    Context,  // change what is being edited, through Model/FileSystem
    Unknown,  // nothing produced this outcome's branch, or it failed before one
};

inline std::string_view kind_name(Kind kind) {
    switch (kind) {
        case Kind::View: return "view";
        case Kind::Change: return "change";
        case Kind::Context: return "context";
        case Kind::Unknown: return "unknown";
    }
    return "unknown";
}

struct Outcome {
    bool ok = true;
    Kind kind = Kind::Unknown;
    std::string message;
    View view{};
};

// The one shape a refusal takes: which kind of instruction it was, and the sentence for the
// user. A view and a change both refuse this way, so the layers that can write a sentence
// build the struct here instead of each in its own words.
inline Outcome refusal(Kind kind, std::string message) { return Outcome{false, kind, std::move(message)}; }
