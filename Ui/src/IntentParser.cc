// The text front end and its boundary: one line in, one structured instruction out. The
// line is split into words, the first of them picks a reader out of the table at the end of
// this file, and that reader turns the rest of the words into an instruction. Every piece
// of syntax lives on this side of the boundary - what a length looks like, what a fill
// looks like, which words make an instruction - so nothing above the ui layer ever sees the
// text a user typed.
#include "IntentParser.hh"

#include <array>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "SvgValue.hh"

namespace {

// Thrown inside this file only, and caught at its boundary: a parser that reports
// halfway through four levels of calls is worse to read than one that says what is
// wrong where it finds out. Nothing outside sees this exception.
struct ParseFailed {
    std::string message;
};

// One line, or the values after an element kind, split into the words an instruction is
// made of.
using Words = std::vector<std::string>;

// One instruction read from the words of one line. Most verbs answer with a type out of
// Intent, and `help` answers with the command list of this layer, which is why a reader
// gives back a whole request rather than an instruction.
using Reader = Request (*)(const Words&);

// One verb: the word that names it, and the reader the rest of the words go to. A table of
// these is the whole vocabulary of the front end, so an instruction is a row rather than a
// branch of a chain, and a word that is in no row is the one complaint this file makes on
// its own.
struct Verb {
    std::string_view name;
    Reader read;
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

// What an instruction's words have to be, or the sentence saying what it takes. The
// sentence names the instruction itself, so it reads the same whether the words counted are
// a whole line or the values that followed an element kind.
void expect(std::size_t count, std::size_t wanted, std::string_view sentence) {
    if (count != wanted) throw ParseFailed{std::string{sentence}};
}

void expect_between(std::size_t count, std::size_t least, std::size_t most, std::string_view sentence) {
    if (count < least || count > most) throw ParseFailed{std::string{sentence}};
}

void expect_least(std::size_t count, std::size_t least, std::string_view sentence) {
    if (count < least) throw ParseFailed{std::string{sentence}};
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

// --- the readers: one per verb, each of them the words in and the instruction out ---

// create <kind> <id> [values] [in <gid>]
Request parse_create(const Words& words) {
    expect_least(words.size(), 3,
                 "create takes an element kind and an id: 'create rect box 0 0 10 10', or 'create g layer'");
    const std::string& kind = words[1];
    const std::string& id = words[2];

    // Where the element goes comes off the end first, so the rest of the line is
    // exactly the values the kind asks for and nothing counts words twice.
    Words values{words.begin() + 3, words.end()};
    NodeRef parent = RootRef{};
    if (values.size() >= 2 && values[values.size() - 2] == "in") {
        parent = reference_of(values.back());
        values.resize(values.size() - 2);
    }

    if (kind == "g") {
        expect(values.size(), 0, "create g takes '<id> [in <gid>]'");
        return CreateGroup{id, parent};
    }
    if (kind == "rect") {
        expect_between(values.size(), 4, 5, "create rect takes '<id> <x> <y> <w> <h> [fill] [in <gid>]'");
        CreateRect intent{id, length_of(values[0]), length_of(values[1]), length_of(values[2]), length_of(values[3]),
                          std::nullopt};
        if (values.size() == 5) intent.fill = color_of(values[4]);
        intent.parent = parent;
        return intent;
    }
    if (kind == "circle") {
        expect_between(values.size(), 3, 4, "create circle takes '<id> <cx> <cy> <r> [fill] [in <gid>]'");
        CreateCircle intent{id, length_of(values[0]), length_of(values[1]), length_of(values[2]), std::nullopt};
        if (values.size() == 4) intent.fill = color_of(values[3]);
        intent.parent = parent;
        return intent;
    }
    if (kind == "ellipse") {
        expect_between(values.size(), 4, 5, "create ellipse takes '<id> <cx> <cy> <rx> <ry> [fill] [in <gid>]'");
        CreateEllipse intent{id,  length_of(values[0]), length_of(values[1]), length_of(values[2]),
                             length_of(values[3]), std::nullopt};
        if (values.size() == 5) intent.fill = color_of(values[4]);
        intent.parent = parent;
        return intent;
    }
    if (kind == "line") {
        expect_between(values.size(), 4, 5, "create line takes '<id> <x1> <y1> <x2> <y2> [stroke] [in <gid>]'");
        CreateLine intent{id,  length_of(values[0]), length_of(values[1]), length_of(values[2]),
                          length_of(values[3]), std::nullopt};
        if (values.size() == 5) intent.stroke = color_of(values[4]);
        intent.parent = parent;
        return intent;
    }
    if (kind == "text") {
        expect(values.size(), 3, "create text takes '<id> <x> <y> \"content\" [in <gid>]'");
        CreateText intent{id, length_of(values[0]), length_of(values[1]), values[2]};
        intent.parent = parent;
        return intent;
    }

    throw ParseFailed{"create can build rect, circle, ellipse, line, text or g, not '" + kind + "'"};
}

Request parse_delete(const Words& words) {
    expect(words.size(), 2, "delete takes '<id>'");
    return Delete{reference_of(words[1])};
}

Request parse_move(const Words& words) {
    expect(words.size(), 4, "move takes '<id> <dx> <dy>'");
    return Move{reference_of(words[1]), length_of(words[2]), length_of(words[3])};
}

Request parse_scale(const Words& words) {
    expect(words.size(), 3, "scale takes '<id> <factor>'");
    return ScaleBy{reference_of(words[1]), factor_of(words[2])};
}

Request parse_group(const Words& words) {
    expect_least(words.size(), 3, "group takes '<newGid> <id> <id> ...': at least one node to put inside");
    Group intent;
    intent.id = words[1];
    for (std::size_t index = 2; index < words.size(); ++index) intent.nodes.push_back(reference_of(words[index]));
    return intent;
}

Request parse_ungroup(const Words& words) {
    expect(words.size(), 2, "ungroup takes '<gid>'");
    return Ungroup{reference_of(words[1])};
}

Request parse_addto(const Words& words) {
    expect(words.size(), 3, "addto takes '<gid> <id>'");
    return AddTo{reference_of(words[1]), reference_of(words[2])};
}

Request parse_front(const Words& words) {
    expect(words.size(), 2, "front takes '<id>'");
    return Front{reference_of(words[1])};
}

Request parse_back(const Words& words) {
    expect(words.size(), 2, "back takes '<id>'");
    return Back{reference_of(words[1])};
}

Request parse_undo(const Words& words) {
    expect(words.size(), 1, "undo takes no operand");
    return Undo{};
}

Request parse_redo(const Words& words) {
    expect(words.size(), 1, "redo takes no operand");
    return Redo{};
}

Request parse_tree(const Words& words) {
    expect_between(words.size(), 1, 2, "tree takes an optional '<id>'");
    ShowTree intent;
    if (words.size() == 2) intent.node = reference_of(words[1]);
    return intent;
}

Request parse_show(const Words& words) {
    expect(words.size(), 2, "show takes '<id>'");
    return Show{reference_of(words[1])};
}

// One word answers two ways here: with nothing after it this is the same read as `show /`,
// because the canvas is the root element's own size, and with a size it is a change.
Request parse_canvas(const Words& words) {
    if (words.size() == 1) return Show{RootRef{}};
    expect_between(words.size(), 2, 3, "canvas takes a new width, with an optional height");
    SetCanvas intent;
    intent.width = length_of(words[1]);
    if (words.size() == 3) intent.height = length_of(words[2]);
    return intent;
}

Request parse_init(const Words& words) {
    expect_between(words.size(), 1, 3, "init takes an optional width and height, 800x600 by default");
    Init intent;
    if (words.size() >= 2) intent.width = length_of(words[1]);
    if (words.size() == 3) intent.height = length_of(words[2]);
    return intent;
}

Request parse_read(const Words& words) {
    expect(words.size(), 2, "read takes '<file.svg>'");
    return Read{words[1]};
}

Request parse_save(const Words& words) {
    expect_between(words.size(), 1, 2, "save takes an optional '<file.svg>', and none means the file it was read from");
    Save intent;
    if (words.size() == 2) intent.path = words[1];
    return intent;
}

// The one instruction that never leaves this layer: what the commands are is the text of
// usage(), and no other layer knows it.
Request parse_help(const Words& words) {
    expect(words.size(), 1, "help takes no operand");
    return Help{};
}

// The vocabulary itself: one row per verb, in the order the help lists them - `canvas` is
// the one row that answers both of the forms the help gives it. Eighteen names to compare
// one word against is a scan, not a lookup structure.
constexpr std::array<Verb, 18> verbs{{
    {"help", parse_help},
    {"tree", parse_tree},
    {"show", parse_show},
    {"canvas", parse_canvas},
    {"create", parse_create},
    {"delete", parse_delete},
    {"move", parse_move},
    {"scale", parse_scale},
    {"group", parse_group},
    {"ungroup", parse_ungroup},
    {"addto", parse_addto},
    {"front", parse_front},
    {"back", parse_back},
    {"undo", parse_undo},
    {"redo", parse_redo},
    {"init", parse_init},
    {"read", parse_read},
    {"save", parse_save},
}};

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
        const Words words = words_of(line);
        if (words.empty()) throw ParseFailed{"an empty instruction"};
        for (const Verb& verb : verbs) {
            if (verb.name == words.front()) return verb.read(words);
        }
        throw ParseFailed{"unknown instruction '" + words.front() + "'; the instructions are\n" + std::string{usage()}};
    } catch (const ParseFailed& failure) {
        return ParseError{failure.message};
    }
}
