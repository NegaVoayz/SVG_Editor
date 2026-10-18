// The Editor itself: one intent in, one tree-level transaction out. What each
// instruction does is in the sources next to this one - Create.cc, Structure.cc,
// Payload.cc, History.cc - and this file is only the question of which of them answers.
#include "Editor.hh"

#include <exception>
#include <expected>
#include <optional>
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
        // illegal - the transaction that was open is gone and the Model rolled it back.
        // The exception text is a diagnostic, not a user-facing message: the protocol
        // only carries the refusal kind.
        static_cast<void>(error);
        return refused(Kind::Change, fault::InternalError{});
    }
}

std::expected<Id, Refusal> Editor::resolve(const NodeRef& reference) const {
    const std::optional<Id> found = look_up(document_, reference);
    if (found) return *found;
    return std::unexpected(Refusal{fault::MissingShape{reference}});
}

std::expected<Id, Refusal> Editor::resolve_container(const NodeRef& reference) const {
    const std::optional<Id> found = look_up(document_, reference);
    if (!found) return std::unexpected(Refusal{fault::MissingContainer{reference}});

    const std::optional<Node> payload = document_.node(*found);
    if (!payload || !is_container(*payload)) {
        return std::unexpected(Refusal{fault::NotContainer{reference}});
    }
    return *found;
}
