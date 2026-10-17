// The boundary of the front end: one line in, one structured instruction out. The line is
// split into words here, the first of them is looked up in the table of verbs, and that
// reader turns the rest of the words into an instruction. A word that is in no row of the
// table is the one refusal this file writes by itself, and the command list is the other
// thing it owns: `help` is answered with the text of this layer, which no other layer knows.
//
// The parts are next to this file: the shape a reader is written in and what one word spells
// are in Syntax, the verbs and their readers in Command, and the element kinds `create`
// builds in Create.
#include "IntentParser.hh"

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Command.hh"
#include "Syntax.hh"

namespace {

// One line, split into the words an instruction is made of. Two things can put a
// space inside a word: a quoted run, and an open bracket - "rgb(0, 255, 0)" is one
// word. A bracket left open runs to the end of the line, which the value parser then
// refuses with a sentence of its own.
Words words_of(std::string_view line) {
    Words words;
    std::size_t index = 0;
    while (index < line.size()) {
        if (std::isspace(static_cast<unsigned char>(line[index])) != 0) {
            ++index;
            continue;
        }

        std::string word;
        if (line[index] == '"') {
            for (++index; index < line.size() && line[index] != '"'; ++index) word.push_back(line[index]);
            if (index < line.size()) ++index;  // the closing quote belongs to the syntax, not to the word
        } else {
            int brackets = 0;
            for (; index < line.size(); ++index) {
                const char character = line[index];
                if (character == '(') ++brackets;
                if (character == ')') --brackets;
                if (brackets <= 0 && std::isspace(static_cast<unsigned char>(character)) != 0) break;
                word.push_back(character);
            }
        }
        words.push_back(std::move(word));
    }
    return words;
}

}  // namespace

std::string_view usage() {
    return "  view     tree [id]              the tree, the whole document by default\n"
           "           show <id>              one element's own attributes\n"
           "           canvas                 the document's size, which is the root's\n"
           "           help                   this list\n"
           "  change   create <kind> <id> [values] [in <gid>]\n"
           "             rect <id> <x> <y> <w> <h> [fill] | circle <id> <cx> <cy> <r> [fill]\n"
           "             ellipse <id> <cx> <cy> <rx> <ry> [fill] | line <id> <x1> <y1> <x2> <y2> [stroke]\n"
           "             text <id> <x> <y> \"content\" | g <id>\n"
           "           delete <id>            the node and everything under it\n"
           "           move <id> <dx> <dy> | scale <id> <factor>\n"
           "           set <id> <attr> <value>   one attribute; id, parent and children are not attributes\n"
           "           group <newGid> <id> <id> ... | ungroup <gid> | addto <gid> <id>\n"
           "           front <id> | back <id>   the ends of the child order, first child in front\n"
           "           canvas <w> [h]         a new size, and the history starts over\n"
           "           undo | redo\n"
           "  context  init [w] [h]           a fresh document, 800x600 by default\n"
           "           read <file.svg>        read a document, naming what it left unnamed\n"
           "           save [file.svg]        write it, and the history starts over\n"
           "  a node is '#<handle>', '/' for the root, or an SVG id\n";
}

Request parse(std::string_view line) {
    try {
        const Words words = words_of(line);
        if (words.empty()) throw ParseFailed{"an empty instruction"};
        for (const Verb& verb : verbs()) {
            if (verb.name == words.front()) return verb.read(words);
        }
        throw ParseFailed{"unknown instruction '" + words.front() + "'; the instructions are\n" + std::string{usage()}};
    } catch (const ParseFailed& failure) {
        return ParseError{failure.message};
    }
}
