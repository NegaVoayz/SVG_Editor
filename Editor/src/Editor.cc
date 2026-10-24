// The Editor itself: one intent in, one tree-level transaction out. What each
// instruction does is in the sources next to this one - Create.cc, Structure.cc,
// Payload.cc, History.cc - and this file is only the question of which of them answers.
#include "Editor.hh"

#include <cstdlib>
#include <exception>
#include <expected>
#include <iostream>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "Exhaustive.hh"
#include "NodeRef.hh"
#include "Outcome.hh"
#include "Translations.hh"

namespace {

template<typename T>
std::optional<event::EditEvent> edit_event_of(const T& value) {
    static_assert(std::is_constructible_v<event::EditEvent, T>,
                  "a non-edit event must be excluded explicitly");
    return event::EditEvent{value};
}

template<>
std::optional<event::EditEvent> edit_event_of<event::Undone>(const event::Undone&) {
    return std::nullopt;
}

template<>
std::optional<event::EditEvent> edit_event_of<event::Redone>(const event::Redone&) {
    return std::nullopt;
}

template<>
std::optional<event::EditEvent> edit_event_of<event::CheckedOut>(const event::CheckedOut&) {
    return std::nullopt;
}

std::optional<event::EditEvent> edit_event_of(const Event& event) {
    return std::visit(
        [](const auto& value) -> std::optional<event::EditEvent> {
            using T = std::decay_t<decltype(value)>;
            return edit_event_of<T>(value);
        },
        event);
}

}  // namespace

Editor::Editor(Document& document) : document_(document) {}

HistoryView Editor::history(std::optional<std::size_t> limit) {
    sync_history();
    return HistoryView{history_events_, history_cursor_, limit.value_or(default_history_limit)};
}

void Editor::refresh_history() {
    sync_history();
}

void Editor::sync_history() {
    // Entries above the checkout belong to the redo tail and have to stay for redo.
    // Only a baseline with no editable history at all makes every entry unreachable.
    if (document_.depth() == 0) history_events_.clear();
    history_cursor_ = event::HistoryCursor{document_.baseline_commit(), document_.commit_id(),
                                            document_.position(), document_.depth()};
}

std::shared_ptr<const event::EditEvent> Editor::history_event(std::size_t position, CommitId commit) const {
    if (position >= history_events_.size()) return nullptr;
    const event::HistoryEntry& entry = history_events_[position];
    if (entry.commit != commit) return nullptr;
    return std::make_shared<const event::EditEvent>(entry.outcome);
}

void Editor::record_history_event(std::size_t position, CommitId commit, event::EditEvent outcome) {
    // A new edit after undo replaces the redo tail and the entries that described it.
    history_events_.resize(position);
    history_events_.push_back(event::HistoryEntry{commit, std::move(outcome), position + 1});
}

// The dispatch: the type of the instruction decides, and Translations.hh is where the
// specializations it may reach are declared. An instruction with no specialization and no
// element to build reaches the primary template, which fails the build naming the type.
Outcome Editor::translate(const ChangeIntent& intent) {
    sync_history();
    const std::size_t before_position = document_.position();
    const CommitId before_commit = document_.commit_id();
    document_.begin_instruction();
    Outcome outcome = refused(Kind::Change, fault::InternalError{});
    try {
        outcome = std::visit([this](const auto& value) { return handle(value); }, intent);
    } catch (const std::exception& error) {
        // Whatever refused - a handle that names nothing, a tree that would become
        // illegal - the transaction that was open is gone and the Model rolled it back.
        // The exception text is a diagnostic, not a user-facing message: the protocol
        // only carries the refusal kind.
        if (const char* debug = std::getenv("SVG_EDITOR_DEBUG"); debug != nullptr && *debug != '\0') {
            std::cerr << "internal edit failure: " << error.what() << '\n';
        }
        outcome = refused(Kind::Change, fault::InternalError{});
    }
    document_.finish_instruction();

    const std::size_t after_position = document_.position();
    const CommitId after_commit = document_.commit_id();
    if (document_.depth() != 0 && outcome.ok() && std::holds_alternative<Event>(outcome.result) &&
        after_commit > before_commit && after_position > 0) {
        // One intent is one commit, so one new id is the only shape an outcome can
        // describe. History navigation reports an event too, but moves the checkout
        // without writing a new block, and is excluded by edit_event_of.
        if (std::optional<event::EditEvent> edit = edit_event_of(outcome.event())) {
            if (after_position != before_position + 1) {
                if (const char* debug = std::getenv("SVG_EDITOR_DEBUG");
                    debug != nullptr && *debug != '\0') {
                    std::cerr << "internal edit failure: one instruction committed more than once\n";
                }
                outcome = refused(Kind::Change, fault::InternalError{});
            } else {
                record_history_event(after_position - 1, after_commit, std::move(*edit));
            }
        }
    }
    sync_history();
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
