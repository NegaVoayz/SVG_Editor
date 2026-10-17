// The Editor itself: one intent in, one tree-level transaction out. What each
// instruction does is in the sources next to this one - Create.cc, Structure.cc,
// Payload.cc, History.cc - and this file is only the question of which of them answers.
#include "Editor.hh"

#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
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
        // back. All that is left is the sentence for the user.
        return refusal(Kind::Change, "the edit was not applied: " + std::string(error.what()));
    }
}

// The second level of the dispatch: a create is one instruction, and which element it builds
// is the next question. Each kind comes back to handle(), where the builder it has is what
// answers it - and the explicit instantiations in Create.cc are what gives that answer a body
// to call from here.
template<> Outcome Editor::handle<Create>(const Create& intent) {
    return std::visit([this](const auto& element) { return handle(element); }, intent);
}

Id Editor::resolve(const NodeRef& reference) const {
    const std::optional<Id> found = look_up(document_, reference);
    if (found) return *found;
    if (std::holds_alternative<HandleRef>(reference)) {
        throw std::invalid_argument("no node lives at handle #" + std::to_string(std::get<HandleRef>(reference).id));
    }
    throw std::invalid_argument("no node is called " + describe(reference));
}
