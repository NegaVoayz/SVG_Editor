#pragma once

#include <string_view>

#include "Document.hh"
#include "Editor.hh"
#include "Protocol.hh"

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
    template<typename T>
    Outcome handle(const T& intent) {
        static_assert(always_false<T>, "the Planner has no route for this intent");
        return Outcome{false, Kind::Unknown, "no route"};
    }

    // the two views: reading the tree and reporting it is the Planner's half
    Outcome show_tree() const;
    Outcome show_find(const Find& intent) const;

    // the two context changes, through the document that owns the file layer
    Outcome open(const Open& intent) const;
    Outcome save(const Save& intent) const;

    Document& document_;
    Editor& editor_;
};
