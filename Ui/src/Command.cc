// Every verb but `create`, and the table that names them all: one reader per verb, the words
// in and the instruction out. A word that is in no row is the one complaint the boundary
// makes on its own, so what the front end accepts is this table and nothing else.
#include "Command.hh"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <string>
#include <string_view>
#include <vector>

#include "Create.hh"  // the one reader that lives with the kinds it reads
#include "Quoting.hh"
#include "Set.hh"     // the one reader that lives with the attributes it reads

namespace {

struct Selectors {
    EditTarget target = EditTarget::Default;
    EditScope scope = EditScope::Default;
};

// The two selectors are independent words, so either order reads the same. A target or
// a scope said twice is refused instead of letting the last one win silently.
Selectors edit_selectors(const Words& words, std::size_t first, std::string_view command) {
    Selectors selectors;
    const std::string owner{command};
    for (std::size_t index = first; index < words.size(); ++index) {
        const std::string& word = words[index];
        if (word == "transform") {
            if (selectors.target != EditTarget::Default) {
                throw ParseFailed{owner + " takes one target selector: transform or params"};
            }
            selectors.target = EditTarget::Transform;
        } else if (word == "params" || word == "direct") {
            if (selectors.target != EditTarget::Default) {
                throw ParseFailed{owner + " takes one target selector: transform or params"};
            }
            selectors.target = EditTarget::Params;
        } else if (word == "cascade") {
            if (selectors.scope != EditScope::Default) {
                throw ParseFailed{owner + " takes one scope selector: cascade or restrict"};
            }
            selectors.scope = EditScope::Cascade;
        } else if (word == "restrict") {
            if (selectors.scope != EditScope::Default) {
                throw ParseFailed{owner + " takes one scope selector: cascade or restrict"};
            }
            selectors.scope = EditScope::Restrict;
        } else {
            throw ParseFailed{"unknown " + owner + " selector " + quote_if_needed(word) +
                              ": expected transform, params, cascade or restrict"};
        }
    }
    if (selectors.target == EditTarget::Transform && selectors.scope != EditScope::Default) {
        throw ParseFailed{"transform does not take cascade or restrict"};
    }
    return selectors;
}

Request parse_delete(const Words& words) {
    expect(words.size(), 2, "delete takes '<id>'");
    return Delete{reference_of(words[1])};
}

Request parse_move(const Words& words) {
    expect_between(words.size(), 4, 6,
                   "move takes '<id> <dx> <dy> [transform|params] [cascade|restrict]'");
    const Selectors selectors = edit_selectors(words, 4, "move");
    return Move{reference_of(words[1]), length_of(words[2]), length_of(words[3]), selectors.target, selectors.scope};
}

Request parse_scale(const Words& words) {
    expect_between(words.size(), 3, 5, "scale takes '<id> <factor> [transform|params] [cascade|restrict]'");
    const Selectors selectors = edit_selectors(words, 3, "scale");
    return ScaleBy{reference_of(words[1]), factor_of(words[2]), selectors.target, selectors.scope};
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

Request parse_checkout(const Words& words) {
    expect_between(words.size(), 2, 3, "checkout takes '<commit>' and an optional timeline count");
    CommitId commit = 0;
    const std::string& word = words[1];
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), commit);
    if (error != std::errc{} || end != word.data() + word.size() || commit == 0) {
        throw ParseFailed{"cannot read '" + word + "' as a commit id"};
    }
    Checkout intent{commit, std::nullopt};
    if (words.size() == 3) {
        std::size_t limit = 0;
        const std::string& count = words[2];
        const auto [count_end, count_error] =
            std::from_chars(count.data(), count.data() + count.size(), limit);
        if (count_error != std::errc{} || count_end != count.data() + count.size()) {
            throw ParseFailed{"cannot read '" + count + "' as a checkout timeline count"};
        }
        intent.limit = limit;
    }
    return intent;
}

Request parse_history(const Words& words) {
    expect_between(words.size(), 1, 2, "history takes an optional count");
    ShowHistory intent;
    if (words.size() == 2) {
        std::size_t limit = 0;
        const std::string& word = words[1];
        const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), limit);
        if (error != std::errc{} || end != word.data() + word.size()) {
            throw ParseFailed{"cannot read '" + word + "' as a history count"};
        }
        intent.limit = limit;
    }
    return intent;
}

Request parse_zoom(const Words& words) {
    expect(words.size(), 2, "zoom takes '<factor>'");
    const float factor = factor_of(words[1]);
    if (factor <= 0.0F) throw ParseFailed{"zoom factor must be greater than zero"};
    return Zoom{factor};
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
    intent.width = maybe_length_percentage(length_of(words[1]));
    if (words.size() == 3) intent.height = maybe_length_percentage(length_of(words[2]));
    return intent;
}

Request parse_init(const Words& words) {
    expect_between(words.size(), 1, 3, "init takes an optional width and height, 800x600 by default");
    Init intent;
    if (words.size() >= 2) intent.width = maybe_length_percentage(length_of(words[1]));
    if (words.size() == 3) intent.height = maybe_length_percentage(length_of(words[2]));
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

Request parse_exit(const Words& words) {
    expect(words.size(), 1, "exit takes no operand");
    return Exit{};
}

// The vocabulary itself: one row per verb, in the order the help lists them - `canvas` is
// the one row that answers both of the forms the help gives it. A table this size is a
// scan, not a lookup structure.
constexpr std::array<Verb, 24> table{{
    {"help", parse_help},
    {"exit", parse_exit},
    {"quit", parse_exit},
    {"tree", parse_tree},
    {"show", parse_show},
    {"history", parse_history},
    {"zoom", parse_zoom},
    {"canvas", parse_canvas},
    {"create", parse_create},
    {"delete", parse_delete},
    {"move", parse_move},
    {"scale", parse_scale},
    {"set", parse_set},
    {"group", parse_group},
    {"ungroup", parse_ungroup},
    {"addto", parse_addto},
    {"front", parse_front},
    {"back", parse_back},
    {"undo", parse_undo},
    {"redo", parse_redo},
    {"checkout", parse_checkout},
    {"init", parse_init},
    {"read", parse_read},
    {"save", parse_save},
}};

}  // namespace

std::span<const Verb> verbs() { return table; }
