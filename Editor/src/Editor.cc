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
            if constexpr (std::is_same_v<T, event::Undone> || std::is_same_v<T, event::Redone> ||
                          std::is_same_v<T, event::CheckedOut>) {
                return std::nullopt;
            } else {
                return event::EditEvent{value};
            }
        },
        event);
}

}  // namespace

Editor::Editor(Document& document) : document_(document) {}

HistoryView Editor::history(std::optional<std::size_t> limit) {
    sync_history_events();

    const std::size_t depth = document_.depth();
    const std::size_t head = document_.position();
    HistoryView view;
    view.baseline = document_.baseline_commit();
    view.head = document_.commit_id();
    view.position = head;
    view.depth = depth;
    const std::size_t cap = limit.value_or(default_history_limit);
    const std::size_t checkpoints = depth + 1;

    // Select `cap` neighboring checkpoints around HEAD, split as evenly as possible. If
    // one side reaches an end of the branch, the other side grows to keep that neighbor
    // count full. HEAD itself is the center row. The baseline, latest commit, and HEAD
    // are anchors and stay visible even when they fall outside the ordinary window.
    std::vector<bool> selected(checkpoints, cap >= checkpoints - 1);
    if (cap < checkpoints - 1 && cap != 0) {
        selected[head] = true;
        const std::size_t above_target = cap / 2;
        std::size_t above = std::min(above_target, depth - head);
        std::size_t below = std::min(cap - above, head);
        std::size_t remaining = cap - above - below;

        const std::size_t extra_below = std::min(remaining, head - below);
        below += extra_below;
        remaining -= extra_below;
        const std::size_t extra_above = std::min(remaining, depth - head - above);
        above += extra_above;

        for (std::size_t offset = 1; offset <= above; ++offset) selected[head + offset] = true;
        for (std::size_t offset = 1; offset <= below; ++offset) selected[head - offset] = true;
    }
    if (cap < checkpoints - 1) {
        selected[0] = true;
        selected[head] = true;
        selected[depth] = true;
    }

    for (std::size_t position = 1; position <= depth; ++position) {
        if (!selected[position]) continue;
        const std::optional<CommitId> commit = document_.commit_at(position - 1);
        if (!commit) continue;
        view.entries.push_back(
            event::HistoryEntry{*commit, history_event(position - 1, *commit), position});
    }
    return view;
}

void Editor::sync_history_events() {
    // Entries above the checkout belong to the redo tail and have to stay for redo.
    // Only a baseline with no editable history at all makes every entry unreachable.
    if (document_.depth() == 0) history_events_.clear();
}

std::shared_ptr<const event::EditEvent> Editor::history_event(std::size_t position, CommitId commit) const {
    if (position >= history_events_.size()) return nullptr;
    const event::HistoryEntry& entry = history_events_[position];
    return entry.commit == commit ? entry.outcome : nullptr;
}

void Editor::record_history_event(std::size_t position, CommitId commit, event::EditEvent outcome) {
    // A new edit after undo replaces the redo tail and the entries that described it.
    history_events_.resize(position);
    history_events_.push_back(
        event::HistoryEntry{commit, std::make_shared<const event::EditEvent>(std::move(outcome))});
}

// The dispatch: the type of the instruction decides, and Translations.hh is where the
// specializations it may reach are declared. An instruction with no specialization and no
// element to build reaches the primary template, which fails the build naming the type.
Outcome Editor::translate(const ChangeIntent& intent) {
    sync_history_events();
    const CommitId before_commit = document_.commit_id();
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
    const CommitId after_commit = document_.commit_id();
    if (document_.depth() == 0) {
        history_events_.clear();
    } else if (outcome.ok() && std::holds_alternative<Event>(outcome.result) &&
               after_commit > before_commit && after_position > 0) {
        // One intent is one commit, so one new id is the only shape an outcome can
        // describe. An empty transaction leaves the id and the log alone.
        if (std::optional<event::EditEvent> edit = edit_event_of(outcome.event())) {
            record_history_event(after_position - 1, after_commit, std::move(*edit));
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
