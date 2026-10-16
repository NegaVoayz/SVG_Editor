#include "IntentParser.hh"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <optional>
#include <sstream>
#include <string>
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

constexpr std::string_view length_syntax =
    "a length is a number, optionally with px, pt, cm, mm, in, em, ex, or a percentage";
constexpr std::string_view color_syntax =
    "a fill is #rgb, #rrggbb, rgb(r,g,b), none or currentColor (named colours have no ids in the model yet)";

std::vector<std::string> words_of(std::string_view line) {
    std::vector<std::string> words;
    std::istringstream stream{std::string(line)};
    for (std::string word; stream >> word; ) {
        // A value may hold spaces inside its brackets: "rgb(0, 255, 0)" is one word,
        // and a bracket left open runs to the end, which the value parser then
        // refuses with its own sentence.
        while (std::count(word.begin(), word.end(), '(') > std::count(word.begin(), word.end(), ')')) {
            std::string rest;
            if (!(stream >> rest)) break;
            word += ' ';
            word += rest;
        }
        words.push_back(std::move(word));
    }
    return words;
}

void expect(const std::vector<std::string>& words, std::size_t count, std::string_view syntax) {
    if (words.size() == count) return;
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

std::optional<Id> parse_handle(std::string_view text) {
    try {
        std::size_t used = 0;
        const unsigned long long value = std::stoull(std::string(text), &used);
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

Intent create_of(const std::vector<std::string>& words) {
    if (words.size() < 2) throw ParseFailed{"create takes an element kind: 'create rect ...' or 'create g <id>'"};

    const std::string& kind = words[1];
    if (kind == "g") {
        expect(words, 3, "'g <id>'");
        return CreateGroup{words[2]};
    }
    if (kind != "rect") throw ParseFailed{"create can build 'rect' or 'g', not '" + kind + "'"};

    if (words.size() != 7 && words.size() != 8) throw ParseFailed{"create takes 'rect <id> <x> <y> <w> <h> [fill]'"};
    CreateRect intent{words[2], length_of(words[3]), length_of(words[4]), length_of(words[5]),
                      length_of(words[6]), std::nullopt};
    if (words.size() == 8) intent.fill = color_of(words[7]);
    return intent;
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
    if (verb == "addto") {
        expect(words, 3, "'<gid> <id>'");
        return AddTo{reference_of(words[1]), reference_of(words[2])};
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
        expect(words, 1, "no operand");
        return ShowTree{};
    }
    if (verb == "find") {
        expect(words, 2, "'<svg-id>'");
        return Find{words[1]};
    }
    if (verb == "open") {
        expect(words, 2, "'<path>'");
        return Open{words[1]};
    }
    if (verb == "save") {
        expect(words, 2, "'<path>'");
        return Save{words[1]};
    }

    throw ParseFailed{"unknown instruction '" + verb + "'; the instructions are\n" + std::string{usage()}};
}

}  // namespace

std::string_view usage() {
    return "  view     tree | find <svg-id>\n"
           "  change   create rect <id> <x> <y> <w> <h> [fill] | create g <id>\n"
           "           delete <id> | move <id> <dx> <dy> | addto <gid> <id> | undo | redo\n"
           "  context  open <path> | save <path>\n"
           "  a node is '#<handle>', '/' for the root, or an SVG id\n";
}

std::variant<Intent, ParseError> parse(std::string_view line) {
    try {
        const std::vector<std::string> words = words_of(line);
        if (words.empty()) throw ParseFailed{"an empty instruction"};
        return parse_words(words);
    } catch (const ParseFailed& failure) {
        return ParseError{failure.message};
    }
}
