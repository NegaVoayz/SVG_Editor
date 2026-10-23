// What the text front end accepts and what it refuses, one line at a time.
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "Command.hh"
#include "Create.hh"
#include "IntentParser.hh"

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::printf("FAIL line %d: %s\n", line, expression);
    ++failures;
}

// One line is an instruction, the front end's own help, or the reason it is neither, so
// reading one goes through two variants; a create adds a third, because its element kind is
// a variant of its own. These few keep the checks readable either way.
template<typename T>
bool holds(const Request& line) {
    if (!std::holds_alternative<Intent>(line)) return false;
    const Intent& intent = std::get<Intent>(line);
    if constexpr (std::is_constructible_v<Create, T>) {
        return std::holds_alternative<Create>(intent) && std::holds_alternative<T>(std::get<Create>(intent));
    } else if constexpr (std::is_constructible_v<Set, T>) {
        return std::holds_alternative<Set>(intent) && std::holds_alternative<T>(std::get<Set>(intent));
    } else {
        return std::holds_alternative<T>(intent);
    }
}

template<typename T>
const T& as(const Request& line) {
    const Intent& intent = std::get<Intent>(line);
    if constexpr (std::is_constructible_v<Create, T>) return std::get<T>(std::get<Create>(intent));
    else if constexpr (std::is_constructible_v<Set, T>) return std::get<T>(std::get<Set>(intent));
    else return std::get<T>(intent);
}

bool refuses(const Request& line) { return std::holds_alternative<ParseError>(line); }

const std::string& complaint(const Request& line) { return std::get<ParseError>(line).message; }

std::string name_in(const NodeRef& reference) { return std::get<SvgIdRef>(reference).name; }

// The table is the vocabulary and the help is its prose, so this is the one pair that can
// still drift: a verb the front end accepts and the help does not name is a failure. The
// names are read out of the table rather than written down a second time here.
void check_help_names_every_verb() {
    const std::string_view help = usage();
    for (const Verb& verb : verbs()) {
        if (help.find(verb.name) != std::string_view::npos) continue;
        std::printf("FAIL the help does not name the verb '%.*s'\n", static_cast<int>(verb.name.size()),
                    verb.name.data());
        ++failures;
    }

    std::string kinds;
    for (const std::string_view kind : element_kind_names()) {
        if (!kinds.empty()) kinds += " | ";
        kinds += kind;
    }
    const std::string line = "             " + kinds + "\n";
    if (help.find(line) != std::string_view::npos) return;
    std::printf("FAIL the help does not list the create table as '%s'\n", line.c_str());
    ++failures;
}

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
        CHECK(!intent.parent_named);
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
        CHECK(as<CreateRect>(placed).parent_named);
        CHECK(name_in(as<CreateRect>(placed).parent) == "layer");
    }

    const Request painted = parse("create rect box 0 0 10 10 #0f0 in layer");
    CHECK(holds<CreateRect>(painted));
    if (holds<CreateRect>(painted)) {
        CHECK(as<CreateRect>(painted).fill.has_value());
        CHECK(name_in(as<CreateRect>(painted).parent) == "layer");
    }

    const Request round = parse("create circle c1 10 20 5 blue");
    CHECK(holds<CreateCircle>(round));
    if (holds<CreateCircle>(round)) {
        CHECK(as<CreateCircle>(round).fill.has_value());
        if (as<CreateCircle>(round).fill) {
            CHECK(std::holds_alternative<Color::Named>(as<CreateCircle>(round).fill->specific));
            CHECK(std::get<Color::Named>(as<CreateCircle>(round).fill->specific).id == Color::NamedColorId::Blue);
        }
    }
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

    // A quoted run can carry a quote or a backslash by escaping it.
    const Request quoted_quote = parse("create text title 20 40 \"say \\\"hi\\\"\"");
    CHECK(holds<CreateText>(quoted_quote));
    if (holds<CreateText>(quoted_quote)) CHECK(as<CreateText>(quoted_quote).content == "say \"hi\"");
    const Request quoted_backslash = parse("set title text \"a\\\\b\"");
    CHECK(holds<SetText>(quoted_backslash));
    if (holds<SetText>(quoted_backslash)) CHECK(as<SetText>(quoted_backslash).value == "a\\b");

    // A line whose first non-space character is '#' is a comment, including any
    // semicolons after it.
    CHECK(std::holds_alternative<Comment>(parse("   # a note; with punctuation")));
    CHECK(std::holds_alternative<Comment>(parse("#comment")));

    const Request group = parse("create g layer");
    CHECK(holds<CreateGroup>(group));
    if (holds<CreateGroup>(group)) CHECK(as<CreateGroup>(group).id == "layer");

    const Request box = parse("create box status 10 20 120 40 \"Ready\" #00ff00 in layer");
    CHECK(holds<CreateBox>(box));
    if (holds<CreateBox>(box)) {
        const CreateBox& intent = as<CreateBox>(box);
        CHECK(intent.id == "status");
        CHECK(intent.label == "Ready");
        CHECK(std::get<Length>(intent.width).value == 120.0F);
        CHECK(intent.fill.has_value());
        CHECK(name_in(intent.parent) == "layer");
    }
    CHECK(refuses(parse("create box status 10 20 120 40")));

    // a '#' word is an SVG id here, not the hive's internal handle
    const Request hash_id = parse("delete #3");
    CHECK(holds<Delete>(hash_id));
    if (holds<Delete>(hash_id)) {
        CHECK(std::holds_alternative<SvgIdRef>(as<Delete>(hash_id).node));
        CHECK(std::get<SvgIdRef>(as<Delete>(hash_id).node).name == "#3");
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
        CHECK(intent.target == EditTarget::Default);
        CHECK(intent.scope == EditScope::Default);
    }

    const Request scaled = parse("scale layer 2.5");
    CHECK(holds<ScaleBy>(scaled));
    if (holds<ScaleBy>(scaled)) CHECK(as<ScaleBy>(scaled).factor == 2.5F);
    CHECK(refuses(parse("scale layer wide")));

    // The two selectors are independent words and read the same in either order.
    const Request transformed = parse("move layer 5 6 transform");
    CHECK(holds<Move>(transformed));
    if (holds<Move>(transformed)) {
        CHECK(as<Move>(transformed).target == EditTarget::Transform);
        CHECK(as<Move>(transformed).scope == EditScope::Default);
    }
    const Request cascaded = parse("scale layer 2 cascade params");
    CHECK(holds<ScaleBy>(cascaded));
    if (holds<ScaleBy>(cascaded)) {
        CHECK(as<ScaleBy>(cascaded).target == EditTarget::Params);
        CHECK(as<ScaleBy>(cascaded).scope == EditScope::Cascade);
    }
    const Request restricted = parse("move box 1 1 restrict");
    CHECK(holds<Move>(restricted));
    if (holds<Move>(restricted)) {
        CHECK(as<Move>(restricted).target == EditTarget::Default);
        CHECK(as<Move>(restricted).scope == EditScope::Restrict);
    }
    CHECK(refuses(parse("move layer 1 1 transform cascade")));
    CHECK(refuses(parse("scale layer 2 transform restrict")));
    CHECK(refuses(parse("move layer 1 1 cascade restrict")));
    CHECK(refuses(parse("move layer 1 1 sideways")));

    // `set` names one attribute at runtime; the name is in the table and decides both the
    // value's syntax and which typed instruction comes out.
    const Request set_x = parse("set box x 50");
    CHECK(holds<SetX>(set_x));
    if (holds<SetX>(set_x)) {
        CHECK(name_in(as<SetX>(set_x).node) == "box");
        CHECK(std::get<Length>(as<SetX>(set_x).value).unit == Length::Unit::UserUnit);
    }

    const Request set_fill = parse("set box fill #00ff00");
    CHECK(holds<SetFill>(set_fill));
    if (holds<SetFill>(set_fill)) {
        CHECK(std::holds_alternative<Color>(as<SetFill>(set_fill).value));
        CHECK(std::holds_alternative<Color::RGB>(std::get<Color>(as<SetFill>(set_fill).value).specific));
    }

    const Request set_class = parse("set box class \"layer one\"");
    CHECK(holds<SetClass>(set_class));
    if (holds<SetClass>(set_class)) CHECK(as<SetClass>(set_class).value == "layer one");

    const Request set_text = parse("set title text \"My Diagram v2\"");
    CHECK(holds<SetText>(set_text));
    if (holds<SetText>(set_text)) CHECK(as<SetText>(set_text).value == "My Diagram v2");

    const Request set_content = parse("set title content \"legacy spelling\"");
    CHECK(holds<SetContent>(set_content));

    const Request set_transform = parse("set layer transform translate(5 6)");
    CHECK(holds<SetTransform>(set_transform));
    if (holds<SetTransform>(set_transform)) {
        const Transform& value = as<SetTransform>(set_transform).value;
        CHECK(value.a == 1.0F);
        CHECK(value.b == 0.0F);
        CHECK(value.c == 0.0F);
        CHECK(value.d == 1.0F);
        CHECK(numeric_value(value.e) == 5.0F);
        CHECK(numeric_value(value.f) == 6.0F);
    }

    const Request set_transform_list = parse("set layer transform \"scale(2 2) rotate(30) scale(3 1) rotate(45)\"");
    CHECK(holds<SetTransform>(set_transform_list));
    if (holds<SetTransform>(set_transform_list)) {
        const Transform& value = as<SetTransform>(set_transform_list).value;
        CHECK(std::fabs(value.a - 2.9671278F) < 1.0e-4F);
        CHECK(std::fabs(value.b - 3.3460652F) < 1.0e-4F);
        CHECK(std::fabs(value.c + 4.3813414F) < 1.0e-4F);
        CHECK(std::fabs(value.d + 0.8965755F) < 1.0e-4F);
        CHECK(numeric_value(value.e) == 0.0F);
        CHECK(numeric_value(value.f) == 0.0F);
    }

    const Request set_transform_forms =
        parse("set layer transform \"rotate(30 10 20) skewX(15) skewY(-5)\"");
    CHECK(holds<SetTransform>(set_transform_forms));
    if (holds<SetTransform>(set_transform_forms)) {
        const Transform& value = as<SetTransform>(set_transform_forms).value;
        CHECK(std::fabs(value.a - 0.8894679F) < 1.0e-4F);
        CHECK(std::fabs(value.b - 0.4125113F) < 1.0e-4F);
        CHECK(std::fabs(value.c + 0.2679492F) < 1.0e-4F);
        CHECK(std::fabs(value.d - 1.0F) < 1.0e-4F);
        CHECK(std::fabs(numeric_value(value.e) - 11.339746F) < 1.0e-3F);
        CHECK(std::fabs(numeric_value(value.f) + 2.320508F) < 1.0e-3F);
    }

    const Request clear_transform = parse("set layer transform none");
    CHECK(holds<SetTransform>(clear_transform));
    if (holds<SetTransform>(clear_transform)) CHECK(is_identity(as<SetTransform>(clear_transform).value));

    const Request empty_transform = parse("set layer transform \"\"");
    CHECK(holds<SetTransform>(empty_transform));
    if (holds<SetTransform>(empty_transform)) CHECK(is_identity(as<SetTransform>(empty_transform).value));

    const Request unknown_attribute = parse("set box colour red");
    CHECK(refuses(unknown_attribute));
    if (refuses(unknown_attribute)) {
        CHECK(complaint(unknown_attribute) == "error: unknown attribute: colour");
    }

    // a name the table knows but this node does not carry gets past the front end: whether
    // the node carries it is a question for the layer that holds the node.
    CHECK(holds<SetR>(parse("set circle r 5")));
    CHECK(refuses(parse("set box x")));
    CHECK(refuses(parse("set box x wide")));
    CHECK(refuses(parse("set layer transform translate(wide 1)")));

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

    // history reads the current branch; checkout takes one of its commit ids.
    const Request all_history = parse("history");
    CHECK(holds<ShowHistory>(all_history));
    if (holds<ShowHistory>(all_history)) CHECK(!as<ShowHistory>(all_history).limit.has_value());
    const Request recent_history = parse("history 2");
    CHECK(holds<ShowHistory>(recent_history));
    if (holds<ShowHistory>(recent_history)) CHECK(as<ShowHistory>(recent_history).limit == 2);
    CHECK(refuses(parse("history two")));
    CHECK(refuses(parse("history -1")));

    const Request checkout = parse("checkout 42");
    CHECK(holds<Checkout>(checkout));
    if (holds<Checkout>(checkout)) {
        CHECK(as<Checkout>(checkout).commit == 42);
        CHECK(!as<Checkout>(checkout).limit.has_value());
    }
    const Request capped_checkout = parse("checkout 42 5");
    CHECK(holds<Checkout>(capped_checkout));
    if (holds<Checkout>(capped_checkout)) {
        CHECK(as<Checkout>(capped_checkout).commit == 42);
        CHECK(as<Checkout>(capped_checkout).limit == 5);
    }
    CHECK(refuses(parse("checkout 0")));
    CHECK(refuses(parse("checkout")));
    CHECK(refuses(parse("checkout 42 many")));

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

    // help and exit never leave the front end: the textual session belongs to this layer.
    CHECK(std::holds_alternative<Help>(parse("help")));
    CHECK(std::holds_alternative<Exit>(parse("exit")));
    CHECK(std::holds_alternative<Exit>(parse("quit")));
    CHECK(refuses(parse("exit now")));
    CHECK(std::holds_alternative<Help>(parse("help")));
    CHECK(refuses(parse("help me")));
    check_help_names_every_verb();

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
    const Request named_fill = parse("create rect r 0 0 1 1 RebeccaPurple");
    CHECK(holds<CreateRect>(named_fill));
    if (holds<CreateRect>(named_fill)) {
        const std::optional<Color>& fill = as<CreateRect>(named_fill).fill;
        CHECK(fill.has_value());
        if (fill) {
            CHECK(std::holds_alternative<Color::Named>(fill->specific));
            CHECK(std::get<Color::Named>(fill->specific).id == Color::NamedColorId::RebeccaPurple);
        }
    }

    // and what it refuses, with the reason
    const Request unknown = parse("frobnicate");
    CHECK(refuses(unknown));
    if (refuses(unknown)) {
        CHECK(complaint(unknown) == "error: unknown instruction \"frobnicate\", try \"help\"");
    }

    CHECK(refuses(parse("   ")));
    CHECK(refuses(parse("delete")));
    CHECK(refuses(parse("create poly p 1 2 3")));
    const Request unknown_kind = parse("create hexagon h 0 0");
    CHECK(refuses(unknown_kind));
    if (refuses(unknown_kind)) {
        CHECK(complaint(unknown_kind) == "error: unknown shape type: hexagon (is the plugin loaded?)");
    }

    const Request bad_length = parse("create rect r wide 0 10 10");
    CHECK(refuses(bad_length));
    if (refuses(bad_length)) CHECK(complaint(bad_length).find("as a length") != std::string::npos);

    CHECK(refuses(parse("create rect r 0 0 10 10furlong")));
    CHECK(refuses(parse("create rect nn nan 0 1 1")));
    CHECK(refuses(parse("create rect nn inf 0 1 1")));
    CHECK(refuses(parse("create rect huge 1e308 0 1 1")));

    const Request bad_fill = parse("create rect r 0 0 10 10 notacolour");
    CHECK(refuses(bad_fill));
    if (refuses(bad_fill)) CHECK(complaint(bad_fill).find("as a fill") != std::string::npos);

    const Request hash_letters = parse("delete #abc");
    CHECK(holds<Delete>(hash_letters));
    if (holds<Delete>(hash_letters)) {
        CHECK(std::holds_alternative<SvgIdRef>(as<Delete>(hash_letters).node));
        CHECK(std::get<SvgIdRef>(as<Delete>(hash_letters).node).name == "#abc");
    }

    if (failures != 0) {
        std::printf("intent_parser: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("intent_parser: all checks passed\n");
    return 0;
}
