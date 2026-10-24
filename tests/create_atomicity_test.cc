// Every create kind is one instruction and one history block, including kinds that
// bring a tree rather than a single element.
#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "Create.hh"
#include "Document.hh"
#include "Editor.hh"
#include "FileSystem.hh"
#include "IntentParser.hh"
#include "Planner.hh"

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::printf("FAIL line %d: %s\n", line, expression);
    ++failures;
}

struct Sample {
    std::string_view kind;
    std::string_view values;
};

// A new kind in the front-end table has to gain a usable sample here; otherwise the
// repository-wide atomicity check stops covering the vocabulary it is meant to cover.
constexpr Sample samples[] = {
    {"rect", "r 0 0 1 1"},
    {"circle", "c 1 1 1"},
    {"ellipse", "e 1 1 2 1"},
    {"line", "l 0 0 1 1"},
    {"text", "t 0 0 \"hello\""},
    {"g", "g"},
    {"box", "b 0 0 10 10 \"label\""},
};

std::optional<Sample> sample_for(std::string_view kind) {
    for (const Sample& sample : samples) {
        if (sample.kind == kind) return sample;
    }
    return std::nullopt;
}

}  // namespace

#define CHECK(condition) check((condition), #condition, __LINE__)

int main() {
    for (const std::string_view kind : element_kind_names()) {
        const std::optional<Sample> sample = sample_for(kind);
        CHECK(sample.has_value());
        if (!sample) continue;

        Document document;
        Editor editor{document};
        FileSystem file_system{document};
        Planner planner{document, editor, file_system};
        document.reset(Unset{}, Unset{});
        const std::size_t before_depth = document.depth();
        const std::size_t before_position = document.position();

        const std::string text = "create " + std::string{kind} + " " + std::string{sample->values};
        const Request request = parse(text);
        CHECK(std::holds_alternative<Intent>(request));
        if (!std::holds_alternative<Intent>(request)) continue;

        const Outcome outcome = planner.dispatch(std::get<Intent>(request));
        CHECK(outcome.ok());
        CHECK(document.depth() == before_depth + 1);
        CHECK(document.position() == before_position + 1);
        CHECK(document.element_count() != 0);

        CHECK(document.undo().size() != 0);
        CHECK(document.position() == before_position);
        CHECK(document.element_count() == 0);
    }

    if (failures != 0) {
        std::printf("create_atomicity: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("create_atomicity: all checks passed\n");
    return 0;
}
