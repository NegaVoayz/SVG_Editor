#pragma once

#include <string>
#include <string_view>

#include "Document.hh"
#include "Protocol.hh"

// The Editor is the write path. The Planner hands it the requests it classified
// as a change, and it turns one of them into one tree-level transaction out of
// Model: everything that mutates the document, the history included, goes through
// here, which is also why undo and redo are Editor operations.
//
// It takes a change intent that a front end has already parsed, so it reads no
// text: what is left of an instruction here is what it means.
//
// A request that cannot be carried out leaves the document untouched: the
// transaction is destroyed without a commit and the Model rolls it back.
class Editor {
public:
    explicit Editor(Document& document);

    // One change intent, one transaction. The dispatch is on the type of the
    // intent, so an intent this cannot translate is a build error rather than a
    // branch that quietly does nothing.
    Outcome translate(const ChangeIntent& intent);

private:
    // One translation per intent. The primary template below is what a missing one
    // falls back to, and it fails the build with the name of the type.
    template<typename T>
    Outcome handle(const T& intent) {
        static_assert(always_false<T>, "the Editor has no translation for this intent");
        return Outcome{false, Kind::Change, "no translation"};
    }

    // A reference turned into a node, or the refusal for one that names nothing.
    Id resolve(const NodeRef& reference) const;

    // One new element inside a parent, which is what every create does: the id has to
    // be free, the body is already built, and the element goes last among the
    // children of the parent the command named.
    Outcome add(const std::string& id, std::string_view kind, const NodeRef& parent, Node payload);

    Outcome create(const CreateRect& intent);
    Outcome create(const CreateCircle& intent);
    Outcome create(const CreateEllipse& intent);
    Outcome create(const CreateLine& intent);
    Outcome create(const CreateText& intent);
    Outcome create(const CreateGroup& intent);
    Outcome remove(const Delete& intent);
    Outcome move(const Move& intent);
    Outcome scale(const ScaleBy& intent);
    Outcome group(const Group& intent);
    Outcome ungroup(const Ungroup& intent);
    Outcome addto(const AddTo& intent);
    Outcome front(const Front& intent);
    Outcome back(const Back& intent);
    Outcome canvas(const SetCanvas& intent);
    Outcome undo();
    Outcome redo();

    Document& document_;
};
