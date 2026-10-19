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
#include "Quoting.hh"
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
            for (++index; index < line.size(); ++index) {
                const char character = line[index];
                if (character == '\\' && index + 1 < line.size()) {
                    const char escaped = line[index + 1];
                    if (escaped == '"' || escaped == '\\') {
                        word.push_back(escaped);
                        ++index;
                        continue;
                    }
                }
                if (character == '"') {
                    ++index;  // the closing quote belongs to the syntax, not to the word
                    break;
                }
                word.push_back(character);
            }
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

bool starts_comment(std::string_view line) {
    const std::size_t first = line.find_first_not_of(" \t\r\n");
    return first != std::string_view::npos && line[first] == '#';
}

std::string_view usage() {
    return "  view     tree [id]              the tree, the whole document by default\n"
           "           show <id>              one element's attributes and parent\n"
           "           history [n]            the last n edits and their commit ids, all by default\n"
           "           canvas                 the document's size, which is the root's\n"
           "           help                   this list\n"
           "  change   create <kind> <id> [values] [in <gid>]\n"
           "             rect <id> <x> <y> <w> <h> [fill] | circle <id> <cx> <cy> <r> [fill]\n"
           "             ellipse <id> <cx> <cy> <rx> <ry> [fill] | line <id> <x1> <y1> <x2> <y2> [stroke]\n"
           "             text <id> <x> <y> \"content\" | g <id>\n"
           "           delete <id>            the node and its descendants\n"
           "           move <id> <dx> <dy> [transform|params] [cascade|restrict]\n"
           "           scale <id> <factor> [transform|params] [cascade|restrict]\n"
           "             params changes values directly; transform changes the node's transform\n"
           "             defaults are params + cascade; a node with a transform uses its transform\n"
           "             a leaf's cascade changes that leaf\n"
           "           set <id> <attr> <value>   one attribute; id, parent and children are not attributes\n"
           "           group <newGid> <id> <id> ... | ungroup <gid> | addto <gid> <id>\n"
           "           front <id> | back <id>   the ends of the child order, last child in front\n"
           "           canvas <w> [h]         a new size, and the history starts over\n"
           "           undo | redo | checkout <commit>\n"
           "  context  init [w] [h]           a fresh document, 800x600 by default\n"
           "           read <file.svg>        read a document, naming what it left unnamed\n"
           "           save [file.svg]        write it, and the history starts over\n"
           "  a node is an SVG id; '/' or the reserved word 'svg' names the root\n"
           "  a line starting with '#' is a comment; quoted text accepts \\\" and \\\\\n";
}

Request parse(std::string_view line) {
    try {
        if (starts_comment(line)) return Comment{};
        const Words words = words_of(line);
        if (words.empty()) throw ParseFailed{"an empty instruction"};
        for (const Verb& verb : verbs()) {
            if (verb.name == words.front()) return verb.read(words);
        }
        throw ParseFailed{"error: unknown instruction " + quoted(words.front()) + ", try \"help\""};
    } catch (const ParseFailed& failure) {
        return ParseError{failure.message};
    }
}
