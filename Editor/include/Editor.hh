#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <variant>

#include "Document.hh"
#include "Intent.hh"
#include "NodeRef.hh"
#include "Outcome.hh"

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
//
// The translations are written in the sources next to this one, one file per thing an
// edit changes - building an element, where a node sits, what a node holds, the history -
// and the list of them is in `Editor/src/Translations.hh`. It is a header of
// declarations and nothing else, because a specialization the compiler has not met
// before the point where std::visit asks for it is not a specialization at all.
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
    //
    // A create is the one instruction that asks twice: `handle<Create>` is a translation of
    // its own, and the visit inside it comes back here for each element kind a create may
    // hold - which is why the kinds are answered by this same question and not by a list.
    template<typename T>
    Outcome handle(const T& intent) {
        if constexpr (requires { new_element(intent); }) {
            return create(intent);
        } else {
            // A plain false, not a dependent one: the branch is only ever reached for a
            // specialization that is not a create, and a static_assert that is false in a
            // discarded branch is what C++23 allows here.
            static_assert(false, "the Editor has no translation for this intent");
            return refusal(Kind::Change, "no translation");
        }
    }

    // A reference turned into a node, or the refusal for one that names nothing.
    Id resolve(const NodeRef& reference) const;

    // A reference that has to name a container: one that exists and can hold shapes or
    // other containers. A text holds character data but is not this kind of container.
    Id resolve_container(const NodeRef& reference) const;

    // What one create brings: the tag a message names it by, the element itself, and the
    // words to put inside it when the element is a text. A text holds character data in
    // a child node rather than in an attribute, which is why that is a second node.
    struct NewElement {
        std::string_view kind;
        Node payload;
        std::optional<std::string> content;
    };

    // One per element kind: what each kind builds. The primary template asks for these by
    // name, so a kind with no overload here does not compile.
    NewElement new_element(const CreateRect& intent) const;
    NewElement new_element(const CreateCircle& intent) const;
    NewElement new_element(const CreateEllipse& intent) const;
    NewElement new_element(const CreateLine& intent) const;
    NewElement new_element(const CreateText& intent) const;
    NewElement new_element(const CreateGroup& intent) const;

    // One create for every kind: the id has to be free, the element goes last among the
    // children of the parent the instruction named, and the body is written once. Which
    // kinds ask for it is the visit in Create.cc, so the list of them is the variant.
    template<typename T>
    Outcome create(const T& intent);

    Document& document_;
};
