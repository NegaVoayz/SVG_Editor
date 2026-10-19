#pragma once

#include "Event.hh"

class Document;

// The name an event reports a node by. Character data is not a user-visible node, so a
// handle that reaches character data is named by the text element that owns it.
event::NodeName name_of(const Document& document, Id node);
