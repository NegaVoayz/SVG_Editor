#include "IntentParser.hh"

#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "SvgValue.hh"

namespace {

// Thrown inside this file only, and caught at its boundary: a parser that reports
// halfway through four levels of calls is worse to read than one that says what is
// wrong where it finds out. Nothing outside sees this exception.
struct ParseFailed {
    std::string message;
};

constexpr std::string_view length_syntax =
    "a length is a number, optionally with px, pt, cm, mm, in, em, ex, or a percentage";
constexpr std::string_view color_syntax =
    "a fill is #rgb, #rrggbb, rgb(r,g,b), none or currentColor (named colours have no ids in the model yet)";
constexpr std::string_view factor_syntax = "a scale is a plain number greater than zero";

// One line, split into the words an instruction is made of. Two things can put a
// space inside a word: a quoted run, and an open bracket - "rgb(0, 255, 0)" is one
// word. A bracket left open runs to the end of the line, which the value parser then
// refuses with a sentence of its own.
std::vector<std::string> words_of(std::string_view line) {
    std::vector<std::string> words;
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

void expect(const std::vector<std::string>& words, std::size_t count, std::string_view syntax) {
    if (words.size() == count) return;
    throw ParseFailed{words.front() + " takes " + std::string{syntax}};
}

void expect_between(const std::vector<std::string>& words, std::size_t least, std::size_t most,
                    std::string_view syntax) {
    if (words.size() >= least && words.size() <= most) return;
    throw ParseFailed{words.front() + " takes " + std::string{syntax}};
}

LengthPercentage length_of(const std::string& word) {
    const std::optional<LengthPercentage> value = SvgValue::parse_length(word);
    if (!value) throw ParseFailed{"cannot read '" + word + "' as a length: " + std::string{length_syntax}};
    return *value;
}

Color color_of(const std::string& word) {
    const std::optional<Color> value = SvgValue::parse_color(word);
    if (!value) throw ParseFailed{"cannot read '" + word + "' as a fill: " + std::string{color_syntax}};
    return *value;
}

float factor_of(const std::string& word) {
    const std::optional<float> value = SvgValue::parse_number(word);
    if (!value) throw ParseFailed{"cannot read '" + word + "' as a scale: " + std::string{factor_syntax}};
    return *value;
}

std::optional<Id> parse_handle(std::string_view text) {
    try {
        std::size_t used = 0;
        const unsigned long long value = std::stoull(std::string{text}, &used);
        if (used != text.size()) return std::nullopt;
        return static_cast<Id>(value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// "#3" is a handle, "/" is the root, and anything else is an SVG id: which of the
// three a word is, is syntax, and asking the document which node it means is not.
NodeRef reference_of(const std::string& word) {
    if (word == "/") return RootRef{};
    if (word.size() > 1 && word.front() == '#') {
        const std::optional<Id> handle = parse_handle(std::string_view{word}.substr(1));
        if (!handle) throw ParseFailed{"'" + word + "' is not a handle: write #<number>"};
        return HandleRef{*handle};
    }
    return SvgIdRef{word};
}

// create <kind> <id> [values] [in <gid>]
Intent create_of(const std::vector<std::string>& words) {
    if (words.size() < 3) {
        throw ParseFailed{"create takes an element kind and an id: 'create rect box 0 0 10 10', or 'create g layer'"};
    }
    const std::string& kind = words[1];
    const std::string& id = words[2];

    // Where the element goes comes off the end first, so the rest of the line is
    // exactly the values the kind asks for and nothing counts words twice.
    std::vector<std::string> values{words.begin() + 3, words.end()};
    NodeRef parent = RootRef{};
    if (values.size() >= 2 && values[values.size() - 2] == "in") {
        parent = reference_of(values.back());
        values.resize(values.size() - 2);
    }

    if (kind == "g") {
        if (!values.empty()) throw ParseFailed{"create g takes '<id> [in <gid>]'"};
        return CreateGroup{id, parent};
    }
    if (kind == "rect") {
        if (values.size() != 4 && values.size() != 5) {
            throw ParseFailed{"create rect takes '<id> <x> <y> <w> <h> [fill] [in <gid>]'"};
        }
        CreateRect intent{id, length_of(values[0]), length_of(values[1]), length_of(values[2]), length_of(values[3]),
                          std::nullopt};
        if (values.size() == 5) intent.fill = color_of(values[4]);
        intent.parent = parent;
        return intent;
    }
    if (kind == "circle") {
        if (values.size() != 3 && values.size() != 4) {
            throw ParseFailed{"create circle takes '<id> <cx> <cy> <r> [fill] [in <gid>]'"};
        }
        CreateCircle intent{id, length_of(values[0]), length_of(values[1]), length_of(values[2]), std::nullopt};
        if (values.size() == 4) intent.fill = color_of(values[3]);
        intent.parent = parent;
        return intent;
    }
    if (kind == "ellipse") {
        if (values.size() != 4 && values.size() != 5) {
            throw ParseFailed{"create ellipse takes '<id> <cx> <cy> <rx> <ry> [fill] [in <gid>]'"};
        }
        CreateEllipse intent{id,  length_of(values[0]), length_of(values[1]), length_of(values[2]),
                             length_of(values[3]), std::nullopt};
        if (values.size() == 5) intent.fill = color_of(values[4]);
        intent.parent = parent;
        return intent;
    }
    if (kind == "line") {
        if (values.size() != 4 && values.size() != 5) {
            throw ParseFailed{"create line takes '<id> <x1> <y1> <x2> <y2> [stroke] [in <gid>]'"};
        }
        CreateLine intent{id,  length_of(values[0]), length_of(values[1]), length_of(values[2]),
                          length_of(values[3]), std::nullopt};
        if (values.size() == 5) intent.stroke = color_of(values[4]);
        intent.parent = parent;
        return intent;
    }
    if (kind == "text") {
        if (values.size() != 3) {
            throw ParseFailed{"create text takes '<id> <x> <y> \"content\" [in <gid>]'"};
        }
        CreateText intent{id, length_of(values[0]), length_of(values[1]), values[2]};
        intent.parent = parent;
        return intent;
    }

    throw ParseFailed{"create can build rect, circle, ellipse, line, text or g, not '" + kind + "'"};
}

Intent parse_words(const std::vector<std::string>& words) {
    const std::string& verb = words.front();

    if (verb == "create") return create_of(words);
    if (verb == "delete") {
        expect(words, 2, "'<id>'");
        return Delete{reference_of(words[1])};
    }
    if (verb == "move") {
        expect(words, 4, "'<id> <dx> <dy>'");
        return Move{reference_of(words[1]), length_of(words[2]), length_of(words[3])};
    }
    if (verb == "scale") {
        expect(words, 3, "'<id> <factor>'");
        return ScaleBy{reference_of(words[1]), factor_of(words[2])};
    }
    if (verb == "group") {
        if (words.size() < 3) throw ParseFailed{"group takes '<newGid> <id> <id> ...': at least one node to put inside"};
        Group intent;
        intent.id = words[1];
        for (std::size_t index = 2; index < words.size(); ++index) intent.nodes.push_back(reference_of(words[index]));
        return intent;
    }
    if (verb == "ungroup") {
        expect(words, 2, "'<gid>'");
        return Ungroup{reference_of(words[1])};
    }
    if (verb == "addto") {
        expect(words, 3, "'<gid> <id>'");
        return AddTo{reference_of(words[1]), reference_of(words[2])};
    }
    if (verb == "front") {
        expect(words, 2, "'<id>'");
        return Front{reference_of(words[1])};
    }
    if (verb == "back") {
        expect(words, 2, "'<id>'");
        return Back{reference_of(words[1])};
    }
    if (verb == "undo") {
        expect(words, 1, "no operand");
        return Undo{};
    }
    if (verb == "redo") {
        expect(words, 1, "no operand");
        return Redo{};
    }
    if (verb == "tree") {
        expect_between(words, 1, 2, "an optional '<id>'");
        ShowTree intent;
        if (words.size() == 2) intent.node = reference_of(words[1]);
        return intent;
    }
    if (verb == "show") {
        expect(words, 2, "'<id>'");
        return Show{reference_of(words[1])};
    }
    if (verb == "canvas") {
        // With no operand this is the same read as `show /`: the canvas is the root
        // element's own size, so there is nothing here for the front end to make up.
        if (words.size() == 1) return Show{RootRef{}};
        expect_between(words, 2, 3, "a new width, with an optional height");
        SetCanvas intent;
        intent.width = length_of(words[1]);
        if (words.size() == 3) intent.height = length_of(words[2]);
        return intent;
    }
    if (verb == "init") {
        expect_between(words, 1, 3, "an optional width and height, 800x600 by default");
        Init intent;
        if (words.size() >= 2) intent.width = length_of(words[1]);
        if (words.size() == 3) intent.height = length_of(words[2]);
        return intent;
    }
    if (verb == "read") {
        expect(words, 2, "'<file.svg>'");
        return Read{words[1]};
    }
    if (verb == "save") {
        expect_between(words, 1, 2, "an optional '<file.svg>', and none means the file it was read from");
        Save intent;
        if (words.size() == 2) intent.path = words[1];
        return intent;
    }

    throw ParseFailed{"unknown instruction '" + verb + "'; the instructions are\n" + std::string{usage()}};
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
        const std::vector<std::string> words = words_of(line);
        if (words.empty()) throw ParseFailed{"an empty instruction"};
        if (words.front() == "help") {
            expect(words, 1, "no operand");
            return Help{};
        }
        return parse_words(words);
    } catch (const ParseFailed& failure) {
        return ParseError{failure.message};
    }
}
