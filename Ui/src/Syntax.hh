#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "IntentParser.hh"  // what a reader gives back
#include "NodeRef.hh"       // how a word names a node
#include "SVGNode.hh"       // the values a word may spell

// The shape of a reader, and the words it reads. Everything here is inside the front end:
// neither the value a word spells nor the sentence a word is refused with is anything the
// layers above ever see.

// One line, or the values after an element kind, split into the words an instruction is
// made of.
using Words = std::vector<std::string>;

// Thrown by a reader and caught at the boundary of this layer: a parser that reports halfway
// through four levels of calls is worse to read than one that says what is wrong where it
// finds out. Nothing outside the front end sees this exception.
struct ParseFailed {
    std::string message;
};

// One instruction read from the words of one line. Most verbs answer with a type out of
// Intent, and `help` answers with the command list of this layer, which is why a reader
// gives back a whole request rather than an instruction.
using Reader = Request (*)(const Words&);

// One verb: the word that names it, and the reader the rest of the words go to.
struct Verb {
    std::string_view name;
    Reader read;
};

// What an instruction's words have to be, or the sentence saying what it takes. The
// sentence names the instruction itself, so it reads the same whether the words counted are
// a whole line or the values that followed an element kind.
void expect(std::size_t count, std::size_t wanted, std::string_view sentence);
void expect_between(std::size_t count, std::size_t least, std::size_t most, std::string_view sentence);
void expect_least(std::size_t count, std::size_t least, std::string_view sentence);

// One word as the value it spells, or the refusal: a length, a fill, a scale factor, and
// the three ways a word names a node.
LengthPercentage length_of(const std::string& word);
Color color_of(const std::string& word);
float factor_of(const std::string& word);
NodeRef reference_of(const std::string& word);
