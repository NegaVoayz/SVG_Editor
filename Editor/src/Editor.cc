// The Editor itself: one intent in, one tree-level transaction out. What each
// instruction does is in the sources next to this one - Create.cc, Structure.cc,
// Payload.cc, History.cc - and this file is only the question of which of them answers.
#include "Editor.hh"

#include <exception>
#include <expected>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "NodeRef.hh"
#include "Outcome.hh"
#include "Translations.hh"

namespace {

std::optional<event::EditEvent> edit_event_of(const Event& event) {
    return std::visit(
        [](const auto& value) -> std::optional<event::EditEvent> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, event::Undone> || std::is_same_v<T, event::Redone>) {
                return std::nullopt;
            } else {
                return event::EditEvent{value};
            }
        },
        event);
}

}  // namespace

Editor::Editor(Document& document) : document_(document) {}

void Editor::sync_history_events() {
    const std::size_t depth = document_.depth();
    if (depth == 0) {
        history_events_.clear();
    } else {
        history_events_.resize(depth);
    }
}

// The dispatch: the type of the instruction decides, and Translations.hh is where the
// specializations it may reach are declared. An instruction with no specialization and no
// element to build reaches the primary template, which fails the build naming the type.
Outcome Editor::translate(const ChangeIntent& intent) {
    sync_history_events();
    const std::size_t before_position = document_.position();
    Outcome outcome = refused(Kind::Change, fault::InternalError{});
    try {
        outcome = std::visit([this](const auto& value) { return handle(value); }, intent);
    } catch (const std::exception& error) {
        // Whatever refused - a handle that names nothing, a tree that would become
        // illegal - the transaction that was open is gone and the Model rolled it back.
        // The exception text is a diagnostic, not a user-facing message: the protocol
        // only carries the refusal kind.
        static_cast<void>(error);
        outcome = refused(Kind::Change, fault::InternalError{});
    }

    const std::size_t after_position = document_.position();
    const std::size_t after_depth = document_.depth();
    if (after_depth == 0) {
        history_events_.clear();
    } else {
        history_events_.resize(after_depth);
        if (outcome.ok() && std::holds_alternative<Event>(outcome.result) &&
            after_position == before_position + 1 && after_position == after_depth) {
            if (std::optional<event::EditEvent> edit = edit_event_of(outcome.event())) {
                history_events_[after_position - 1] =
                    std::make_shared<const event::EditEvent>(std::move(*edit));
            }
        }
    }
    return outcome;
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
