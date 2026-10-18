#pragma once

#include <string>
#include <string_view>
#include <variant>

#include "Intent.hh"

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

// A line whose first non-space character is '#'. It is not an instruction: it documents
// the session and asks the pipeline for nothing.
struct Comment {};

// What one line asked for: an instruction for the pipeline, the front end's own help,
// a comment, or the reason the line could not be read.
using Request = std::variant<Intent, Help, Comment, ParseError>;

// The instructions this front end understands, for a usage line and for the
// complaint an unknown word gets.
std::string_view usage();

// Whether the first non-space character is '#', which makes the rest of the line a
// comment rather than an instruction or a semicolon-separated list of them.
bool starts_comment(std::string_view line);

// One line in, an intent or the reason it could not be read. A quoted run is one word,
// so a text element's content may hold spaces, and \" and \\ are the escapes that let
// it hold a quote or a backslash.
Request parse(std::string_view line);
