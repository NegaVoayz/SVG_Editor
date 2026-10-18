#pragma once

#include <type_traits>

#include "Document.hh"
#include "Editor.hh"
#include "Intent.hh"
#include "Outcome.hh"
#include "View.hh"

// The Planner decides what one instruction means, and nothing else:
//
//   - a view reads the document through Model's viewing API and reports;
//   - a change goes to the Editor, which turns it into a tree transaction;
//   - a context change goes to Model/FileSystem, through Document::open()/save(),
//     because it changes which document is being edited, not the tree in it.
//
// The route is a specialization per intent, and the primary template below is a
// build error: an instruction with no route cannot be forgotten, only not written.
class Planner {
public:
    Planner(Document& document, Editor& editor);

    Outcome dispatch(const Intent& intent);

private:
    // Every instruction that is a change goes to the Editor, and which instructions
    // those are is what ChangeIntent holds - so the compiler answers it from the type
    // and no instruction is listed here twice. An instruction that is not a change has
    // to have a route of its own below, and one that has neither reaches the build
    // error, which names its type.
    template<typename T>
    Outcome handle(const T& intent) {
        if constexpr (std::is_constructible_v<ChangeIntent, T>) {
            if (!document_.has_document()) return no_document(Kind::Change);
            return editor_.translate(ChangeIntent{intent});
        } else {
            static_assert(false, "the Planner has no route for this intent");
            return refused(Kind::Unknown, fault::InternalError{});
        }
    }

    Document& document_;
    Editor& editor_;
};
