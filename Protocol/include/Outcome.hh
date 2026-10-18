#pragma once

#include <string_view>
#include <utility>
#include <variant>

#include "Event.hh"
#include "Refusal.hh"
#include "View.hh"

// What comes back out of the pipeline: which branch produced it, and one of the three
// answers that branch can give - a view, an event saying what changed, or a refusal saying
// why nothing happened. None of the three is prose; the ui layer writes the words.

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
    Kind kind;
    std::variant<View, Event, Refusal> result;

    bool ok() const { return !std::holds_alternative<Refusal>(result); }
    const View& view() const { return std::get<View>(result); }
    const Event& event() const { return std::get<Event>(result); }
    const Refusal& refusal() const { return std::get<Refusal>(result); }
};

inline Outcome viewed(Kind kind, View view) { return Outcome{kind, std::move(view)}; }
inline Outcome happened(Kind kind, Event event) { return Outcome{kind, std::move(event)}; }
inline Outcome refused(Kind kind, Refusal refusal) { return Outcome{kind, std::move(refusal)}; }

// The one refusal every command but `init` and `read` shares before a document exists.
inline Outcome no_document(Kind kind) {
    return refused(kind, fault::NoDocument{});
}
