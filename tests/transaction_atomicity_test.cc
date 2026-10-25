// Every change verb, not only create, is one user history step. Undo, redo and checkout
// move an existing checkout instead of committing, so they must leave depth unchanged.
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <variant>

#include "Document.hh"
#include "Editor.hh"
#include "FileSystem.hh"
#include "Intent.hh"
#include "Planner.hh"

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::printf("FAIL line %d: %s\n", line, expression);
    ++failures;
}

LengthPercentage units(float value) { return Length{value, Length::Unit::UserUnit}; }

Outcome dispatch(Planner& planner, const ChangeIntent& intent) {
    return std::visit([&planner](const auto& value) { return planner.dispatch(Intent{value}); }, intent);
}

bool dispatch_ok(Planner& planner, const Intent& intent) { return planner.dispatch(intent).ok(); }

struct EditSample {
    std::string_view verb;
    std::function<void(Planner&)> setup;
    ChangeIntent intent;
};

#define CHECK(condition) check((condition), #condition, __LINE__)

void check_one_commit(const EditSample& sample) {
    Document document;
    Editor editor{document};
    FileSystem file_system{document};
    Planner planner{document, editor, file_system};
    CHECK(dispatch_ok(planner, Init{}));
    sample.setup(planner);

    const std::size_t before_position = document.position();
    const std::size_t before_depth = document.depth();
    const std::size_t before_elements = document.element_count();
    const CommitId before_commit = document.commit_id();
    const Outcome outcome = dispatch(planner, sample.intent);
    CHECK(outcome.ok());

    if (std::holds_alternative<SetCanvas>(sample.intent)) {
        // `canvas` commits once and then makes that state the new baseline.
        CHECK(document.commit_id() > before_commit);
        CHECK(document.depth() == 0);
        CHECK(document.position() == 0);
        return;
    }

    CHECK(document.depth() == before_depth + 1);
    CHECK(document.position() == before_position + 1);
    CHECK(document.commit_id() > before_commit);
    static_cast<void>(document.undo());
    CHECK(document.position() == before_position);
    CHECK(document.depth() == before_depth + 1);
    CHECK(document.element_count() == before_elements);
}

}  // namespace

int main() {
    const EditSample samples[] = {
        {"delete",
         [](Planner& planner) {
             CHECK(dispatch_ok(planner,
                               Create{CreateRect{"victim", units(0), units(0), units(1), units(1), std::nullopt}}));
         },
         Delete{SvgIdRef{"victim"}}},
        {"move",
         [](Planner& planner) {
             CHECK(dispatch_ok(planner,
                               Create{CreateRect{"moving", units(0), units(0), units(1), units(1), std::nullopt}}));
         },
         Move{SvgIdRef{"moving"}, units(2), units(3), EditTarget::Params, EditScope::Restrict}},
        {"scale",
         [](Planner& planner) {
             CHECK(dispatch_ok(planner,
                               Create{CreateRect{"scaled", units(0), units(0), units(1), units(1), std::nullopt}}));
         },
         ScaleBy{SvgIdRef{"scaled"}, 2.0F, EditTarget::Params, EditScope::Restrict}},
        {"group",
         [](Planner& planner) {
             CHECK(dispatch_ok(planner, Create{CreateGroup{"outer"}}));
             CHECK(dispatch_ok(planner, Create{CreateRect{"a", units(0), units(0), units(1), units(1), std::nullopt,
                                                          SvgIdRef{"outer"}, true}}));
             CHECK(dispatch_ok(planner, Create{CreateRect{"b", units(0), units(0), units(1), units(1), std::nullopt,
                                                          SvgIdRef{"outer"}, true}}));
         },
         Group{"pair", {SvgIdRef{"a"}, SvgIdRef{"b"}}}},
        {"ungroup",
         [](Planner& planner) {
             CHECK(dispatch_ok(planner, Create{CreateGroup{"outer"}}));
             CHECK(dispatch_ok(planner, Create{CreateRect{"a", units(0), units(0), units(1), units(1), std::nullopt,
                                                          SvgIdRef{"outer"}, true}}));
             CHECK(dispatch_ok(planner, Create{CreateRect{"b", units(0), units(0), units(1), units(1), std::nullopt,
                                                          SvgIdRef{"outer"}, true}}));
             CHECK(dispatch_ok(planner, Group{"pair", {SvgIdRef{"a"}, SvgIdRef{"b"}}}));
         },
         Ungroup{SvgIdRef{"pair"}}},
        {"addto",
         [](Planner& planner) {
             CHECK(dispatch_ok(planner, Create{CreateGroup{"target"}}));
             CHECK(dispatch_ok(planner,
                               Create{CreateRect{"item", units(0), units(0), units(1), units(1), std::nullopt}}));
         },
         AddTo{SvgIdRef{"target"}, SvgIdRef{"item"}}},
        {"front",
         [](Planner& planner) {
             CHECK(dispatch_ok(planner, Create{CreateGroup{"outer"}}));
             CHECK(dispatch_ok(planner, Create{CreateRect{"a", units(0), units(0), units(1), units(1), std::nullopt,
                                                          SvgIdRef{"outer"}, true}}));
             CHECK(dispatch_ok(planner, Create{CreateRect{"b", units(0), units(0), units(1), units(1), std::nullopt,
                                                          SvgIdRef{"outer"}, true}}));
         },
         Front{SvgIdRef{"a"}}},
        {"back",
         [](Planner& planner) {
             CHECK(dispatch_ok(planner, Create{CreateGroup{"outer"}}));
             CHECK(dispatch_ok(planner, Create{CreateRect{"a", units(0), units(0), units(1), units(1), std::nullopt,
                                                          SvgIdRef{"outer"}, true}}));
             CHECK(dispatch_ok(planner, Create{CreateRect{"b", units(0), units(0), units(1), units(1), std::nullopt,
                                                          SvgIdRef{"outer"}, true}}));
         },
         Back{SvgIdRef{"b"}}},
        {"set",
         [](Planner& planner) {
             CHECK(dispatch_ok(planner,
                               Create{CreateRect{"painted", units(0), units(0), units(1), units(1), std::nullopt}}));
         },
         Set{SetFill{SvgIdRef{"painted"}, Paint{Color{Color::Named{Color::NamedColorId::Red}}}}}},
        {"canvas", [](Planner&) {},
         SetCanvas{maybe_length_percentage(units(1024)), maybe_length_percentage(units(768))}},
    };

    for (const EditSample& sample : samples) check_one_commit(sample);

    // History navigation changes the checkout and never adds a history block.
    {
        Document document;
        Editor editor{document};
        FileSystem file_system{document};
        Planner planner{document, editor, file_system};
        CHECK(dispatch_ok(planner, Init{}));
        CHECK(dispatch_ok(planner,
                          Create{CreateRect{"history", units(0), units(0), units(1), units(1), std::nullopt}}));
        const std::size_t depth = document.depth();

        CHECK(dispatch(planner, Undo{}).ok());
        CHECK(document.depth() == depth);
        CHECK(dispatch(planner, Redo{}).ok());
        CHECK(document.depth() == depth);

        const CommitId current = document.commit_id();
        CHECK(dispatch(planner, Checkout{current}).ok());
        CHECK(document.depth() == depth);

        CHECK(dispatch_ok(planner, Zoom{2.0F}));
        CHECK(document.position() == 1);
        CHECK(document.depth() == depth);
    }

    if (failures != 0) {
        std::printf("transaction_atomicity: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("transaction_atomicity: all checks passed\n");
    return 0;
}
