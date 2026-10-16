#pragma once

#include <span>

#include "Syntax.hh"

// The vocabulary of the front end, and the only place a verb is written down: the source
// next to this header holds one reader per verb, and the first word of a line is what picks
// one out of it.
std::span<const Verb> verbs();
