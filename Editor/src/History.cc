// The two edits that are not of the tree at all. Undo and redo are the Model's, and the
// Editor is the only thing that may ask for them: the history is what makes an edit
// undoable, so it belongs on the same side of the boundary as the edits themselves.
#include <string>

#include "Translations.hh"

// The parameter has no name: undo and redo are instructions with nothing in them, and a
// name for nothing reads as if there were something.
template<> Outcome Editor::handle<Undo>(const Undo&) {
    if (!document_.can_undo()) return refusal(Kind::Change, "there is nothing left to undo");

    document_.undo();
    return Outcome{true, Kind::Change, "undone, at " + std::to_string(document_.position()) + " of " +
                                           std::to_string(document_.depth()) + " edits"};
}

template<> Outcome Editor::handle<Redo>(const Redo&) {
    if (!document_.can_redo()) return refusal(Kind::Change, "there is nothing left to redo");

    document_.redo();
    return Outcome{true, Kind::Change, "redone, at " + std::to_string(document_.position()) + " of " +
                                           std::to_string(document_.depth()) + " edits"};
}
