// The two edits that are not of the tree at all. Undo and redo are the Model's, and the
// Editor is the only thing that may ask for them: the history is what makes an edit
// undoable, so it belongs on the same side of the boundary as the edits themselves.
#include <utility>

#include "Outcome.hh"
#include "Translations.hh"

// The parameter has no name: undo and redo are instructions with nothing in them, and a
// name for nothing reads as if there were something.
template<> Outcome Editor::handle<Undo>(const Undo&) {
    sync_history_events();
    if (!document_.can_undo()) {
        if (document_.undo_barred_by_io()) return refused(Kind::Change, fault::UndoAfterIo{});
        return refused(Kind::Change, fault::NothingToUndo{});
    }

    const std::size_t original_position = document_.position() - 1;
    const std::optional<CommitId> original_commit = document_.commit_at(original_position);
    TouchedNodes touched = document_.undo();
    std::shared_ptr<const event::EditEvent> original =
        original_commit ? history_event(original_position, *original_commit) : nullptr;
    return happened(Kind::Change,
                    event::Undone{document_.position(), document_.depth(), std::move(touched), std::move(original),
                                  document_.commit_id()});
}

template<> Outcome Editor::handle<Redo>(const Redo&) {
    sync_history_events();
    if (!document_.can_redo()) return refused(Kind::Change, fault::NothingToRedo{});

    TouchedNodes touched = document_.redo();
    const std::size_t entered_position = document_.position() - 1;
    const std::optional<CommitId> entered_commit = document_.commit_at(entered_position);
    std::shared_ptr<const event::EditEvent> original =
        entered_commit ? history_event(entered_position, *entered_commit) : nullptr;
    return happened(Kind::Change,
                    event::Redone{document_.position(), document_.depth(), std::move(touched), std::move(original),
                                  document_.commit_id()});
}
