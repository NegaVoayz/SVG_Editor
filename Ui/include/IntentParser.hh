#pragma once

#include <string>
#include <string_view>
#include <variant>

#include "Protocol.hh"

// The text front end's half of the pipeline: one line in, one structured
// instruction out. Every piece of syntax lives on this side of the boundary - what
// a length looks like, what a fill looks like, which words make an instruction - so
// nothing above the ui layer ever sees the text a user typed.
struct ParseError {
    std::string message;
};

// The instructions this front end understands, for a usage line and for the
// complaint an unknown word gets.
std::string_view usage();

// One line in, an intent or the reason it could not be read. Quoting and escaping
// are still missing, so an SVG id cannot hold a space.
std::variant<Intent, ParseError> parse(std::string_view line);
