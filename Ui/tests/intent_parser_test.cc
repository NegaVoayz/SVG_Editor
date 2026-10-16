// What the text front end accepts and what it refuses, one line at a time.
#include <cstdio>
#include <optional>
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

// One line is an instruction, the front end's own help, or the reason it is neither,
// so reading one goes through two variants; these few keep the checks readable.
template<typename T>
bool holds(const Request& line) {
    return std::holds_alternative<Intent>(line) && std::holds_alternative<T>(std::get<Intent>(line));
}

template<typename T>
const T& as(const Request& line) {
    return std::get<T>(std::get<Intent>(line));
}

bool refuses(const Request& line) { return std::holds_alternative<ParseError>(line); }

const std::string& complaint(const Request& line) { return std::get<ParseError>(line).message; }

std::string name_in(const NodeRef& reference) { return std::get<SvgIdRef>(reference).name; }

}  // namespace

#define CHECK(condition) check((condition), #condition, __LINE__)

int main() {
    // a rect with everything, and one without a fill: both go under the root, which is
    // what an instruction says by saying nothing about where
    const Request full = parse("create rect box 0 0 10 10 #ff0000");
    CHECK(holds<CreateRect>(full));
    if (holds<CreateRect>(full)) {
        const CreateRect& intent = as<CreateRect>(full);
        CHECK(intent.id == "box");
        CHECK(std::get<Length>(intent.x).value == 0.0F);
        CHECK(std::get<Length>(intent.height).value == 10.0F);
        CHECK(intent.fill.has_value());
        CHECK(std::holds_alternative<Color::RGB>(intent.fill->specific));
        CHECK(std::get<Color::RGB>(intent.fill->specific).r == 255);
        CHECK(std::holds_alternative<RootRef>(intent.parent));
    }

    const Request bare = parse("create rect r 1 2 3 4");
    CHECK(holds<CreateRect>(bare));
    if (holds<CreateRect>(bare)) CHECK(!as<CreateRect>(bare).fill.has_value());

    // `in <gid>` is where the element goes, and it comes after whatever the kind asks
    // for - with or without the paint in front of it
    const Request placed = parse("create rect box 0 0 10 10 in layer");
    CHECK(holds<CreateRect>(placed));
    if (holds<CreateRect>(placed)) {
        CHECK(!as<CreateRect>(placed).fill.has_value());
        CHECK(std::holds_alternative<SvgIdRef>(as<CreateRect>(placed).parent));
        CHECK(name_in(as<CreateRect>(placed).parent) == "layer");
    }

    const Request painted = parse("create rect box 0 0 10 10 #0f0 in layer");
    CHECK(holds<CreateRect>(painted));
    if (holds<CreateRect>(painted)) {
        CHECK(as<CreateRect>(painted).fill.has_value());
        CHECK(name_in(as<CreateRect>(painted).parent) == "layer");
    }

    const Request round = parse("create circle c1 10 20 5 blue");
    CHECK(refuses(round));  // named colours have no ids in the model yet
    const Request circle = parse("create circle c1 10 20 5 rgb(0, 0, 255)");
    CHECK(holds<CreateCircle>(circle));
    if (holds<CreateCircle>(circle)) {
        CHECK(as<CreateCircle>(circle).id == "c1");
        CHECK(std::get<Length>(as<CreateCircle>(circle).r).value == 5.0F);
        CHECK(as<CreateCircle>(circle).fill.has_value());
    }

    const Request ellipse = parse("create ellipse e1 0 0 30 10");
    CHECK(holds<CreateEllipse>(ellipse));
    if (holds<CreateEllipse>(ellipse)) CHECK(std::get<Length>(as<CreateEllipse>(ellipse).ry).value == 10.0F);

    // a line takes a stroke rather than a fill
    const Request line = parse("create line l1 0 0 10 10 #123456");
    CHECK(holds<CreateLine>(line));
    if (holds<CreateLine>(line)) {
        CHECK(as<CreateLine>(line).stroke.has_value());
        CHECK(std::get<Length>(as<CreateLine>(line).x2).value == 10.0F);
    }

    // A text is a quoted run, so its words may hold spaces, and it is one instruction
    // even though the model writes two nodes for it.
    const Request words = parse("create text title 20 40 \"hello world\" in layer");
    CHECK(holds<CreateText>(words));
    if (holds<CreateText>(words)) {
        CHECK(as<CreateText>(words).content == "hello world");
        CHECK(name_in(as<CreateText>(words).parent) == "layer");
    }
    CHECK(refuses(parse("create text title 20 40")));

    const Request group = parse("create g layer");
    CHECK(holds<CreateGroup>(group));
    if (holds<CreateGroup>(group)) CHECK(as<CreateGroup>(group).id == "layer");

    // the three ways to name a node, and the values SVG writes
    const Request by_handle = parse("delete #3");
    CHECK(holds<Delete>(by_handle));
    if (holds<Delete>(by_handle)) {
        CHECK(std::holds_alternative<HandleRef>(as<Delete>(by_handle).node));
        CHECK(std::get<HandleRef>(as<Delete>(by_handle).node).id == 3U);
    }

    const Request by_root = parse("delete /");
    CHECK(holds<Delete>(by_root));
    if (holds<Delete>(by_root)) CHECK(std::holds_alternative<RootRef>(as<Delete>(by_root).node));

    const Request moved = parse("move layer 5px 50%");
    CHECK(holds<Move>(moved));
    if (holds<Move>(moved)) {
        const Move& intent = as<Move>(moved);
        CHECK(std::holds_alternative<SvgIdRef>(intent.node));
        CHECK(std::get<SvgIdRef>(intent.node).name == "layer");
        CHECK(std::get<Length>(intent.dx).unit == Length::Unit::Px);
        CHECK(std::get<Percentage>(intent.dy).value == 50.0F);
    }

    const Request scaled = parse("scale layer 2.5");
    CHECK(holds<ScaleBy>(scaled));
    if (holds<ScaleBy>(scaled)) CHECK(as<ScaleBy>(scaled).factor == 2.5F);
    CHECK(refuses(parse("scale layer wide")));

    const Request grouped = parse("group pair box ruler");
    CHECK(holds<Group>(grouped));
    if (holds<Group>(grouped)) {
        CHECK(as<Group>(grouped).id == "pair");
        CHECK(as<Group>(grouped).nodes.size() == 2);
    }
    CHECK(refuses(parse("group pair")));  // nothing to put inside it

    CHECK(holds<Ungroup>(parse("ungroup pair")));
    CHECK(holds<AddTo>(parse("addto g1 r1")));
    CHECK(holds<Front>(parse("front box")));
    CHECK(holds<Back>(parse("back box")));
    CHECK(holds<Undo>(parse("undo")));
    CHECK(holds<Redo>(parse("redo")));

    // the views
    CHECK(holds<ShowTree>(parse("tree")));
    const Request subtree = parse("tree layer");
    CHECK(holds<ShowTree>(subtree));
    if (holds<ShowTree>(subtree)) CHECK(name_in(as<ShowTree>(subtree).node) == "layer");
    CHECK(holds<Show>(parse("show box")));
    CHECK(refuses(parse("tree layer extra")));

    // `canvas` with nothing is the canvas read, which is the root's own size
    const Request shown_canvas = parse("canvas");
    CHECK(holds<Show>(shown_canvas));
    if (holds<Show>(shown_canvas)) CHECK(std::holds_alternative<RootRef>(as<Show>(shown_canvas).node));

    const Request resized = parse("canvas 1024 768");
    CHECK(holds<SetCanvas>(resized));
    if (holds<SetCanvas>(resized)) {
        CHECK(std::get<Length>(*as<SetCanvas>(resized).width).value == 1024.0F);
        CHECK(std::get<Length>(*as<SetCanvas>(resized).height).value == 768.0F);
    }
    const Request widened = parse("canvas 800");
    CHECK(holds<SetCanvas>(widened));
    if (holds<SetCanvas>(widened)) {
        CHECK(as<SetCanvas>(widened).width.has_value());
        CHECK(!as<SetCanvas>(widened).height.has_value());
    }

    // the context changes
    const Request fresh = parse("init");
    CHECK(holds<Init>(fresh));
    if (holds<Init>(fresh)) {
        CHECK(!as<Init>(fresh).width.has_value());
        CHECK(!as<Init>(fresh).height.has_value());
    }
    const Request sized = parse("init 400mm 300mm");
    CHECK(holds<Init>(sized));
    if (holds<Init>(sized)) CHECK(std::get<Length>(*as<Init>(sized).height).unit == Length::Unit::Mm);

    CHECK(holds<Read>(parse("read plan.svg")));
    const Request keep = parse("save");
    CHECK(holds<Save>(keep));
    if (holds<Save>(keep)) CHECK(!as<Save>(keep).path.has_value());
    const Request write = parse("save plan.svg");
    CHECK(holds<Save>(write));
    if (holds<Save>(write)) CHECK(as<Save>(write).path.has_value());

    // help never leaves the front end: what the commands are is this layer's own text
    CHECK(std::holds_alternative<Help>(parse("help")));
    CHECK(refuses(parse("help me")));

    // rgb() and the short hex form, with the spaces a function may hold
    const Request function = parse("create rect r 0 0 1 1 rgb(0, 255, 0)");
    CHECK(holds<CreateRect>(function));
    if (holds<CreateRect>(function)) {
        const std::optional<Color>& fill = as<CreateRect>(function).fill;
        CHECK(fill.has_value());
        CHECK(std::holds_alternative<Color::FuncRGB>(fill->specific));
        CHECK(std::get<Color::FuncRGB>(fill->specific).g == 255);
    }
    CHECK(holds<CreateRect>(parse("create rect r 0 0 1 1 #0f0")));

    // and what it refuses, with the reason
    const Request unknown = parse("frobnicate");
    CHECK(refuses(unknown));
    if (refuses(unknown)) CHECK(complaint(unknown).find("unknown instruction") != std::string::npos);

    CHECK(refuses(parse("   ")));
    CHECK(refuses(parse("delete")));
    CHECK(refuses(parse("create poly p 1 2 3")));

    const Request bad_length = parse("create rect r wide 0 10 10");
    CHECK(refuses(bad_length));
    if (refuses(bad_length)) CHECK(complaint(bad_length).find("as a length") != std::string::npos);

    CHECK(refuses(parse("create rect r 0 0 10 10furlong")));

    const Request bad_fill = parse("create rect r 0 0 10 10 lavender");
    CHECK(refuses(bad_fill));
    if (refuses(bad_fill)) CHECK(complaint(bad_fill).find("as a fill") != std::string::npos);

    CHECK(refuses(parse("delete #abc")));

    if (failures != 0) {
        std::printf("intent_parser: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("intent_parser: all checks passed\n");
    return 0;
}
