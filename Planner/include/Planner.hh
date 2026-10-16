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

    // the views: reading the tree and reporting it is the Planner's half of them
    Outcome show_tree(const ShowTree& intent) const;
    Outcome show_node(const Show& intent) const;

    // The context changes: which document is being edited, rather than what is in it.
    // `init` is one of these because it replaces the document, file and all, instead
    // of editing the tree in it - the canvas it may carry comes with the new root.
    Outcome init(const Init& intent);
    Outcome read(const Read& intent) const;
    Outcome save(const Save& intent);

    Document& document_;
    Editor& editor_;
};
