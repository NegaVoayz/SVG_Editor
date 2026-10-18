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
    if (!document_.can_undo()) {
        if (document_.undo_barred_by_io()) return refused(Kind::Change, fault::UndoAfterIo{});
        return refused(Kind::Change, fault::NothingToUndo{});
    }

    TouchedNodes touched = document_.undo();
    return happened(Kind::Change, event::Undone{document_.position(), document_.depth(), std::move(touched)});
}

template<> Outcome Editor::handle<Redo>(const Redo&) {
    if (!document_.can_redo()) return refused(Kind::Change, fault::NothingToRedo{});

    TouchedNodes touched = document_.redo();
    return happened(Kind::Change, event::Redone{document_.position(), document_.depth(), std::move(touched)});
}
