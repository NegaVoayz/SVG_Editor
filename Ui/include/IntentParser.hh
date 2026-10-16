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

// The one instruction that never leaves this layer: what the commands are is the
// front end's own business, so the front end answers this one itself.
struct Help {};

// What one line asked for: an instruction for the pipeline, the front end's own help,
// or the reason the line could not be read.
using Request = std::variant<Intent, Help, ParseError>;

// The instructions this front end understands, for a usage line and for the
// complaint an unknown word gets.
std::string_view usage();

// One line in, an intent or the reason it could not be read. A quoted run is one
// word, so a text element's content may hold spaces; escaping is still missing, so
// there is no way to write a quote inside one.
Request parse(std::string_view line);
