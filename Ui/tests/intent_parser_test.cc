// What the text front end accepts and what it refuses, one line at a time.
#include <cstdio>
#include <string>
#include <variant>

#include "IntentParser.hh"

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::printf("FAIL line %d: %s\n", line, expression);
    ++failures;
}

using Line = std::variant<Intent, ParseError>;

// One line can be any of the instructions or the reason it is not one, so reading it
// goes through two variants; these two keep the checks readable.
template<typename T>
bool holds(const Line& line) {
    return std::holds_alternative<Intent>(line) && std::holds_alternative<T>(std::get<Intent>(line));
}

template<typename T>
const T& as(const Line& line) {
    return std::get<T>(std::get<Intent>(line));
}

const std::string& complaint(const Line& line) { return std::get<ParseError>(line).message; }

}  // namespace

#define CHECK(condition) check((condition), #condition, __LINE__)

int main() {
    // a rect with everything, and one without a fill
    const Line full = parse("create rect box 0 0 10 10 #ff0000");
    CHECK(holds<CreateRect>(full));
    if (holds<CreateRect>(full)) {
        const CreateRect& intent = as<CreateRect>(full);
        CHECK(intent.id == "box");
        CHECK(std::get<Length>(intent.x).value == 0.0F);
        CHECK(std::get<Length>(intent.height).value == 10.0F);
        CHECK(intent.fill.has_value());
        CHECK(std::holds_alternative<Color::RGB>(intent.fill->specific));
        CHECK(std::get<Color::RGB>(intent.fill->specific).r == 255);
    }

    const Line bare = parse("create rect r 1 2 3 4");
    CHECK(holds<CreateRect>(bare));
    if (holds<CreateRect>(bare)) CHECK(!as<CreateRect>(bare).fill.has_value());

    const Line group = parse("create g layer");
    CHECK(holds<CreateGroup>(group));
    if (holds<CreateGroup>(group)) CHECK(as<CreateGroup>(group).id == "layer");

    // the three ways to name a node, and the values SVG writes
    const Line by_handle = parse("delete #3");
    CHECK(holds<Delete>(by_handle));
    if (holds<Delete>(by_handle)) {
        CHECK(std::holds_alternative<HandleRef>(as<Delete>(by_handle).node));
        CHECK(std::get<HandleRef>(as<Delete>(by_handle).node).id == 3U);
    }

    const Line by_root = parse("delete /");
    CHECK(holds<Delete>(by_root));
    if (holds<Delete>(by_root)) {
        CHECK(std::holds_alternative<RootRef>(as<Delete>(by_root).node));
    }

    const Line moved = parse("move layer 5px 50%");
    CHECK(holds<Move>(moved));
    if (holds<Move>(moved)) {
        const Move& intent = as<Move>(moved);
        CHECK(std::holds_alternative<SvgIdRef>(intent.node));
        CHECK(std::get<SvgIdRef>(intent.node).name == "layer");
        CHECK(std::get<Length>(intent.dx).unit == Length::Unit::Px);
        CHECK(std::get<Percentage>(intent.dy).value == 50.0F);
    }

    CHECK(holds<AddTo>(parse("addto g1 r1")));
    CHECK(holds<Undo>(parse("undo")));
    CHECK(holds<Redo>(parse("redo")));
    CHECK(holds<ShowTree>(parse("tree")));
    CHECK(holds<Show>(parse("show box")));
    CHECK(holds<Read>(parse("read a.svg")));
    CHECK(holds<Save>(parse("save a.svg")));

    // rgb() and the short hex form
    const Line function = parse("create rect r 0 0 1 1 rgb(0, 255, 0)");
    CHECK(holds<CreateRect>(function));
    if (holds<CreateRect>(function)) {
        const std::optional<Color>& fill = as<CreateRect>(function).fill;
        CHECK(fill.has_value());
        CHECK(std::holds_alternative<Color::FuncRGB>(fill->specific));
        CHECK(std::get<Color::FuncRGB>(fill->specific).g == 255);
    }
    CHECK(holds<CreateRect>(parse("create rect r 0 0 1 1 #0f0")));

    // and what it refuses, with the reason
    const Line unknown = parse("frobnicate");
    CHECK(std::holds_alternative<ParseError>(unknown));
    if (std::holds_alternative<ParseError>(unknown)) {
        CHECK(complaint(unknown).find("unknown instruction") != std::string::npos);
    }

    CHECK(std::holds_alternative<ParseError>(parse("   ")));
    CHECK(std::holds_alternative<ParseError>(parse("delete")));
    CHECK(std::holds_alternative<ParseError>(parse("tree please")));

    const Line bad_length = parse("create rect r wide 0 10 10");
    CHECK(std::holds_alternative<ParseError>(bad_length));
    if (std::holds_alternative<ParseError>(bad_length)) {
        CHECK(complaint(bad_length).find("as a length") != std::string::npos);
    }

    CHECK(std::holds_alternative<ParseError>(parse("create rect r 0 0 10 10furlong")));

    const Line bad_fill = parse("create rect r 0 0 10 10 lavender");
    CHECK(std::holds_alternative<ParseError>(bad_fill));
    if (std::holds_alternative<ParseError>(bad_fill)) {
        CHECK(complaint(bad_fill).find("as a fill") != std::string::npos);
    }

    CHECK(std::holds_alternative<ParseError>(parse("delete #abc")));

    if (failures != 0) {
        std::printf("intent_parser: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("intent_parser: all checks passed\n");
    return 0;
}
