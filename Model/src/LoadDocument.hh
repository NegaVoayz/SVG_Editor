#pragma once

#include "ParsedSvg.hh"
#include "TreeStore.hh"

// Replace a store with a fully staged document. The parse must have succeeded before
// this runs; the load then commits bounded chunks and finally makes the tree the
// history baseline.
void load_parsed(TreeStore& store, const ParsedSvg& parsed);
