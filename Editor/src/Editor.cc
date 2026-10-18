// The Editor itself: one intent in, one tree-level transaction out. What each
// instruction does is in the sources next to this one - Create.cc, Structure.cc,
// Payload.cc, History.cc - and this file is only the question of which of them answers.
#include "Editor.hh"

#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include "NodeRef.hh"
#include "Outcome.hh"
#include "Translations.hh"

Editor::Editor(Document& document) : document_(document) {}

// The dispatch: the type of the instruction decides, and Translations.hh is where the
// specializations it may reach are declared. An instruction with no specialization and no
// element to build reaches the primary template, which fails the build naming the type.
Outcome Editor::translate(const ChangeIntent& intent) {
    try {
        return std::visit([this](const auto& value) { return handle(value); }, intent);
    } catch (const std::exception& error) {
        // Whatever refused - a handle that names nothing, a tree that would become
        // illegal - the transaction that was open is gone and the Model rolled it
        // back. All that is left is the sentence for the user. A sentence that already
        // names its kind keeps it; one from a lower layer gets the same prefix here.
        std::string message = error.what();
        if (!message.starts_with("error: ")) message = "error: " + message;
        return refusal(Kind::Change, std::move(message));
    }
}

Id Editor::resolve(const NodeRef& reference) const {
    const std::optional<Id> found = look_up(document_, reference);
    if (found) return *found;
    throw std::invalid_argument("error: no such shape: " + label(reference));
}

Id Editor::resolve_container(const NodeRef& reference) const {
    const std::optional<Id> found = look_up(document_, reference);
    if (!found) throw std::invalid_argument("error: no such container: " + label(reference));

    const std::optional<Node> payload = document_.node(*found);
    if (!payload || !is_container(*payload)) {
        throw std::invalid_argument("error: not a container: " + label(reference));
    }
    return *found;
}
