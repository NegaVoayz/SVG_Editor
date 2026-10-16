#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <variant>

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
    // Which translation answers an instruction is a question about the type of the
    // instruction, and this is the whole of the answer: an instruction that brings an
    // element is answered by create(), one body for every kind of element, and an
    // instruction that brings none has to have a translation of its own. An instruction
    // with neither reaches the build error below, which names its type: an unimplemented
    // case is a compile error rather than a branch that quietly does nothing.
    template<typename T>
    Outcome handle(const T& intent) {
        if constexpr (requires { new_element(intent); }) {
            return create(intent);
        } else {
            static_assert(always_false<T>, "the Editor has no translation for this intent");
            return Outcome{false, Kind::Change, "no translation"};
        }
    }

    // A reference turned into a node, or the refusal for one that names nothing.
    Id resolve(const NodeRef& reference) const;

    // What one create brings: the tag a message names it by, the element itself, and the
    // words to put inside it when the element is a text. A text holds character data in
    // a child node rather than in an attribute, which is why that is a second node.
    struct NewElement {
        std::string_view kind;
        Node payload;
        std::optional<std::string> content;
    };

    // One per element kind: these overloads are what says which instructions build an
    // element, and what each of them builds.
    NewElement new_element(const CreateRect& intent) const;
    NewElement new_element(const CreateCircle& intent) const;
    NewElement new_element(const CreateEllipse& intent) const;
    NewElement new_element(const CreateLine& intent) const;
    NewElement new_element(const CreateText& intent) const;
    NewElement new_element(const CreateGroup& intent) const;

    // One create for every kind: the id has to be free, the element goes last among the
    // children of the parent the instruction named, and the body is written once. The
    // instantiations at the end of the source are the list of kinds this can build.
    template<typename T>
    Outcome create(const T& intent);

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
