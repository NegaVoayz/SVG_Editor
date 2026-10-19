// The two edits that are not of the tree at all. Undo and redo are the Model's, and the
// Editor is the only thing that may ask for them: the history is what makes an edit
// undoable, so it belongs on the same side of the boundary as the edits themselves.
#include <string>
#include <utility>
#include <vector>

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

    const std::size_t original_index = document_.position() - 1;
    TouchedNodes touched = document_.undo();
    std::shared_ptr<const event::EditEvent> original;
    if (original_index < history_events_.size()) original = history_events_[original_index];
    return happened(Kind::Change,
                    event::Undone{document_.position(), document_.depth(), std::move(touched), std::move(original)});
}

template<> Outcome Editor::handle<Redo>(const Redo&) {
    sync_history_events();
    if (!document_.can_redo()) return refused(Kind::Change, fault::NothingToRedo{});

    const std::size_t original_index = document_.position();
    TouchedNodes touched = document_.redo();
    std::shared_ptr<const event::EditEvent> original;
    if (original_index < history_events_.size()) original = history_events_[original_index];
    return happened(Kind::Change,
                    event::Redone{document_.position(), document_.depth(), std::move(touched), std::move(original)});
}
