// The `create` verb, and the element kinds it can build: one builder per kind, a table of
// them, and the one reader that looks a kind up in that table.
#include "Create.hh"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

// One kind: the word that names it, and what it makes of the id, the values and the place
// the line named. The values are what follows the kind with `in <gid>` already off the end,
// so a builder reads exactly what its own kind asks for. What comes back is a Create, which
// is the one instruction the rest of the pipeline sees.
struct ElementKind {
    std::string_view name;
    Create (*build)(const std::string& id, const Words& values, const NodeRef& parent);
};

Create build_group(const std::string& id, const Words& values, const NodeRef& parent) {
    expect(values.size(), 0, "create g takes '<id> [in <gid>]'");
    return CreateGroup{id, parent};
}

Create build_rect(const std::string& id, const Words& values, const NodeRef& parent) {
    expect_between(values.size(), 4, 5, "create rect takes '<id> <x> <y> <w> <h> [fill] [in <gid>]'");
    CreateRect intent{id, length_of(values[0]), length_of(values[1]), length_of(values[2]), length_of(values[3]),
                      std::nullopt};
    if (values.size() == 5) intent.fill = color_of(values[4]);
    intent.parent = parent;
    return intent;
}

Create build_circle(const std::string& id, const Words& values, const NodeRef& parent) {
    expect_between(values.size(), 3, 4, "create circle takes '<id> <cx> <cy> <r> [fill] [in <gid>]'");
    CreateCircle intent{id, length_of(values[0]), length_of(values[1]), length_of(values[2]), std::nullopt};
    if (values.size() == 4) intent.fill = color_of(values[3]);
    intent.parent = parent;
    return intent;
}

Create build_ellipse(const std::string& id, const Words& values, const NodeRef& parent) {
    expect_between(values.size(), 4, 5, "create ellipse takes '<id> <cx> <cy> <rx> <ry> [fill] [in <gid>]'");
    CreateEllipse intent{id,  length_of(values[0]), length_of(values[1]), length_of(values[2]),
                         length_of(values[3]), std::nullopt};
    if (values.size() == 5) intent.fill = color_of(values[4]);
    intent.parent = parent;
    return intent;
}

// A line has no inside to fill, so the one colour it takes is its stroke.
Create build_line(const std::string& id, const Words& values, const NodeRef& parent) {
    expect_between(values.size(), 4, 5, "create line takes '<id> <x1> <y1> <x2> <y2> [stroke] [in <gid>]'");
    CreateLine intent{id,  length_of(values[0]), length_of(values[1]), length_of(values[2]),
                      length_of(values[3]), std::nullopt};
    if (values.size() == 5) intent.stroke = color_of(values[4]);
    intent.parent = parent;
    return intent;
}

Create build_text(const std::string& id, const Words& values, const NodeRef& parent) {
    expect(values.size(), 3, "create text takes '<id> <x> <y> \"content\" [in <gid>]'");
    CreateText intent{id, length_of(values[0]), length_of(values[1]), values[2]};
    intent.parent = parent;
    return intent;
}

// The kinds a `create` may name, in the order the help lists them.
constexpr std::array<ElementKind, 6> kinds{{
    {"rect", build_rect},
    {"circle", build_circle},
    {"ellipse", build_ellipse},
    {"line", build_line},
    {"text", build_text},
    {"g", build_group},
}};

}  // namespace

// create <kind> <id> [values] [in <gid>]
Request parse_create(const Words& words) {
    expect_least(words.size(), 3,
                 "create takes an element kind and an id: 'create rect box 0 0 10 10', or 'create g layer'");
    const std::string& id = words[2];

    // Where the element goes comes off the end first, so the rest of the line is
    // exactly the values the kind asks for and nothing counts words twice.
    Words values{words.begin() + 3, words.end()};
    NodeRef parent = RootRef{};
    if (values.size() >= 2 && values[values.size() - 2] == "in") {
        parent = reference_of(values.back());
        values.resize(values.size() - 2);
    }

    for (const ElementKind& kind : kinds) {
        if (kind.name == words[1]) return Intent{kind.build(id, values, parent)};
    }
    throw ParseFailed{"error: unknown shape type: " + words[1] + " (is the plugin loaded?)"};
}
