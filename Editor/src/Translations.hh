#pragma once

#include <string>
#include <string_view>

#include "Editor.hh"
#include "Intent.hh"

// Every instruction the Editor answers with a translation of its own, and which source
// writes it down. They are declared here, and not in Editor.hh, because they are what
// the Editor does rather than what it offers - but they do have to be declared
// somewhere before the visitor in Editor.cc asks for them: a specialization the compiler
// has not met by the point where std::visit instantiates handle<T> is not a
// specialization at all, and what comes out instead is the primary template's build
// error. The element kinds are not on this list: their one instruction is, and one body
// answers all six of them.

// Create.cc: the second level - a create is one instruction, and its element kind is the
// next question. The kinds themselves come back through the primary template, which is
// what asks for a builder by name.
template<> Outcome Editor::handle<Create>(const Create& intent);

// Structure.cc: where a node sits, and whether it is there at all.
template<> Outcome Editor::handle<Delete>(const Delete& intent);
template<> Outcome Editor::handle<AddTo>(const AddTo& intent);
template<> Outcome Editor::handle<Front>(const Front& intent);
template<> Outcome Editor::handle<Back>(const Back& intent);
template<> Outcome Editor::handle<Group>(const Group& intent);
template<> Outcome Editor::handle<Ungroup>(const Ungroup& intent);

// Payload.cc: what one node holds, and where it sits is not part of it.
template<> Outcome Editor::handle<Move>(const Move& intent);
template<> Outcome Editor::handle<ScaleBy>(const ScaleBy& intent);
template<> Outcome Editor::handle<SetCanvas>(const SetCanvas& intent);

// Set.cc: one named attribute of one node. `Set` is a second-level variant, like `Create`,
// and its attributes are answered by overloads in that file rather than by this list.
template<> Outcome Editor::handle<Set>(const Set& intent);

// History.cc: the edits that are not of the tree at all.
template<> Outcome Editor::handle<Undo>(const Undo& intent);
template<> Outcome Editor::handle<Redo>(const Redo& intent);
template<> Outcome Editor::handle<Checkout>(const Checkout& intent);

// The one element more than one file builds. A command builds its own element, but the
// three that rearrange the tree - `group` in particular - have to make one out of nothing
// to have somewhere to put what they take, so it is declared here and defined next to the
// other builders in Create.cc.
Node make_group(std::string svg_id);

// Every user edit takes the next id from the process coordinator and hands it to
// MVCCHive explicitly. The hive's native commit id is then the shared checkout id,
// including for an empty commit a second hive needs in order to stay aligned.
inline void commit_edit(Transaction& edit) { edit.commit(next_commit_id()); }
