#pragma once

#include "Syntax.hh"

// The reader of `set`, which lives with the attributes it looks up: the name of an
// attribute decides the value's syntax, so the table here is also the list of words a
// `set` line may name.
Request parse_set(const Words& words);
