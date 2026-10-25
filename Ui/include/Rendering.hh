#pragma once

#include <string>

#include "Outcome.hh"

// The other direction of the same boundary the parser guards: the Planner hands over
// what it read, and the words around it - what an element is called, how a length is
// written, how deep a line is indented - are chosen here. Every kind of node,
// container, shape, colour and transform is named by an overload of its own, so a
// kind added to the model is a build error here until it is named too.
std::string render(const TreeView& view);
std::string render(const NodeView& view);
std::string render(const HistoryView& view);
std::string render(const ZoomView& view);
std::string render(const View& view);
std::string render(const Event& event);
std::string render(const Refusal& refusal);

// The whole answer. Every branch is plain prose; a refusal carries the `error:` prefix.
std::string render(const Outcome& outcome);
