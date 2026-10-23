#pragma once

#include <string>

#include "Document.hh"

// Serialize the current checkout of a document as UTF-8 SVG. The tree has already
// passed Model's invariants, so this is the value-to-syntax half of file I/O.
std::string write_svg(const Document& document);
