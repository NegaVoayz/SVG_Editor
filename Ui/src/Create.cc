// The `create` verb, and the element kinds it can build: one builder per kind, a table of
// them, and the one reader that looks a kind up in that table.
#include "Create.hh"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "Exhaustive.hh"
#include "Quoting.hh"

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

Create build_box(const std::string& id, const Words& values, const NodeRef& parent) {
    expect_between(values.size(), 5, 6,
                   "create box takes '<id> <x> <y> <w> <h> \"label\" [fill] [in <gid>]'");
    CreateBox intent{id, length_of(values[0]), length_of(values[1]), length_of(values[2]),
                     length_of(values[3]), values[4], std::nullopt};
    if (values.size() == 6) intent.fill = color_of(values[5]);
    intent.parent = parent;
    return intent;
}

// The kinds a `create` may name, in the order the help lists them. The array size is
// deduced from the rows, so adding an initializer cannot leave an empty lookup row.
constexpr auto kinds = std::to_array<ElementKind>({
    {"rect", build_rect},
    {"circle", build_circle},
    {"ellipse", build_ellipse},
    {"line", build_line},
    {"text", build_text},
    {"g", build_group},
    {"box", build_box},
});

constexpr auto kind_names = [] {
    std::array<std::string_view, kinds.size()> names;
    for (std::size_t index = 0; index < kinds.size(); ++index) names[index] = kinds[index].name;
    return names;
}();

}  // namespace

std::span<const std::string_view> element_kind_names() { return kind_names; }

// create <kind> <id> [values] [in <gid>]
Request parse_create(const Words& words) {
    expect_least(words.size(), 3,
                 "create takes an element kind and an id: 'create rect box 0 0 10 10', or 'create g layer'");
    const std::string& id = words[2];

    // Where the element goes comes off the end first, so the rest of the line is
    // exactly the values the kind asks for and nothing counts words twice.
    Words values{words.begin() + 3, words.end()};
    NodeRef parent = RootRef{};
    bool parent_named = false;
    if (values.size() >= 2 && values[values.size() - 2] == "in") {
        parent = reference_of(values.back());
        parent_named = true;
        values.resize(values.size() - 2);
    }

    for (const ElementKind& kind : kinds) {
        if (kind.name == words[1]) {
            Create made = kind.build(id, values, parent);
            std::visit(
                [parent_named](auto& create) {
                    using T = std::remove_cvref_t<decltype(create)>;
                    if constexpr (requires { create.parent_named = parent_named; }) {
                        create.parent_named = parent_named;
                    } else {
                        static_assert(always_false<T>, "a create intent has no parent_named field");
                    }
                },
                made);
            return Intent{std::move(made)};
        }
    }
    throw ParseFailed{"error: unknown shape type: " + quote_if_needed(words[1]) + " (is the plugin loaded?)"};
}
