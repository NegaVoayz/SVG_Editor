#pragma once

#include "Syntax.hh"

// The reader of `create`, which lives with the element kinds it looks up rather than with
// the other readers: which kinds there are is a table of its own.
Request parse_create(const Words& words);
