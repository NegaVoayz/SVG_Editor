// The pipeline end to end, from the side a front end drives it: a structured
// instruction goes in at the Planner, and a view, a change or a context change
// comes back out. Parsing is not in here - the text front end has its own test for
// that - because an intent is already structured when it arrives.
#include <cstdio>
#include <string>

#include "Document.hh"
#include "Editor.hh"
#include "Planner.hh"
#include "Protocol.hh"

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::printf("FAIL line %d: %s\n", line, expression);
    ++failures;
}

LengthPercentage units(float value) { return Length{value, Length::Unit::UserUnit}; }

}  // namespace

#define CHECK(condition) check((condition), #condition, __LINE__)

int main() {
    Document document;
    Editor editor{document};
    Planner planner{document, editor};

    // a view is served by reading the tree
    const Outcome view = planner.dispatch(ShowTree{});
    CHECK(view.ok);
    CHECK(view.kind == Kind::View);
    CHECK(view.message.find("<svg>") != std::string::npos);

    // a change goes through the Editor, and one instruction is one transaction
    const Outcome created = planner.dispatch(CreateGroup{"layer"});
    CHECK(created.ok);
    CHECK(created.kind == Kind::Change);
    CHECK(document.children(document.root()).size() == 1);
    CHECK(document.find("layer").has_value());

    const Outcome shape = planner.dispatch(
        CreateRect{"box", units(0), units(0), units(10), units(10), Color{Color::RGB{255, 0, 0}}});
    CHECK(shape.ok);
    CHECK(document.children(document.root()).size() == 2);

    // addto re-parents the shape, and move only changes its geometry: the tree
    // shows both, because the view prints a shape's coordinates
    const Outcome attached = planner.dispatch(AddTo{SvgIdRef{"layer"}, SvgIdRef{"box"}});
    CHECK(attached.ok);
    const Outcome moved = planner.dispatch(Move{SvgIdRef{"box"}, units(5), units(5)});
    CHECK(moved.ok);

    const Outcome found = planner.dispatch(Find{"layer"});
    CHECK(found.ok);
    CHECK(found.kind == Kind::View);
    CHECK(found.message.front() == '#');

    const Outcome tree = planner.dispatch(ShowTree{});
    CHECK(tree.ok);
    CHECK(tree.message.find("id=\"layer\"") != std::string::npos);
    CHECK(tree.message.find("x=\"5\" y=\"5\" w=\"10\" h=\"10\" fill=\"#ff0000\"") != std::string::npos);

    // One undo step is one instruction, in order: the move first, then the addto
    // that put the shape under the group, then the create that made it.
    const Outcome undone_move = planner.dispatch(Undo{});
    CHECK(undone_move.ok);
    const Outcome undone_attach = planner.dispatch(Undo{});
    CHECK(undone_attach.ok);
    CHECK(document.children(document.root()).size() == 2);
    const Outcome undone_create = planner.dispatch(Undo{});
    CHECK(undone_create.ok);
    CHECK(document.find("box") == std::nullopt);
    CHECK(document.children(document.root()).size() == 1);

    // and one redo brings that one instruction back
    const Outcome redone_create = planner.dispatch(Redo{});
    CHECK(redone_create.ok);
    CHECK(document.find("layer").has_value());
    CHECK(document.find("box").has_value());
    CHECK(document.children(document.root()).size() == 2);

    // a context change is the Model's file layer, and it reports that there is no
    // parser yet instead of pretending it opened something
    const Outcome missing = planner.dispatch(Open{"no-such-file.svg"});
    CHECK(!missing.ok);
    CHECK(missing.kind == Kind::Context);
    CHECK(missing.message.find("cannot open") != std::string::npos);

    const Outcome unparsed = planner.dispatch(Open{std::string(SVG_EDITOR_SOURCE_DIR) + "/Structure.md"});
    CHECK(!unparsed.ok);
    CHECK(unparsed.kind == Kind::Context);
    CHECK(unparsed.message.find("not implemented") != std::string::npos);

    // A reference that names nothing is refused by the layer that owns the write
    // path, and the document is left exactly as it was.
    const Outcome bad_handle = planner.dispatch(Move{HandleRef{9999}, units(1), units(1)});
    CHECK(!bad_handle.ok);
    CHECK(bad_handle.kind == Kind::Change);
    CHECK(bad_handle.message.find("no node lives at handle #9999") != std::string::npos);

    const Outcome bad_name = planner.dispatch(Delete{SvgIdRef{"nothing"}});
    CHECK(!bad_name.ok);
    CHECK(bad_name.message.find("no node is called \"nothing\"") != std::string::npos);

    // and so is what the model refuses to do with a node that does exist
    const Outcome not_a_container = planner.dispatch(AddTo{SvgIdRef{"box"}, SvgIdRef{"layer"}});
    CHECK(!not_a_container.ok);
    CHECK(not_a_container.message.find("cannot hold children") != std::string::npos);
    CHECK(document.children(document.root()).size() == 2);

    const Outcome rootless = planner.dispatch(Delete{RootRef{}});
    CHECK(!rootless.ok);
    CHECK(rootless.message.find("root cannot be erased") != std::string::npos);
    CHECK(document.find("layer").has_value());

    // a translation that does not add up is refused before anything is written
    const Outcome mixed_units = planner.dispatch(Move{SvgIdRef{"box"}, Length{1.0F, Length::Unit::Mm}, units(0)});
    CHECK(!mixed_units.ok);
    CHECK(mixed_units.message.find("does not add up") != std::string::npos);

    // an id that is taken is refused, and one that is free is accepted
    const Outcome taken = planner.dispatch(CreateGroup{"box"});
    CHECK(!taken.ok);
    CHECK(taken.message.find("already called") != std::string::npos);
    CHECK(planner.dispatch(CreateGroup{"other"}).ok);

    // Moving a container is its transform, because a group has no coordinates of its
    // own: the first move becomes a translate, and the tree writes it out.
    const Outcome moved_group = planner.dispatch(Move{SvgIdRef{"layer"}, units(3), units(4)});
    CHECK(moved_group.ok);
    const Outcome moved_again = planner.dispatch(Move{SvgIdRef{"layer"}, units(1), units(1)});
    CHECK(moved_again.ok);
    const Outcome after_moves = planner.dispatch(ShowTree{});
    CHECK(after_moves.message.find("transform=\"translate(4 5)\"") != std::string::npos);
    CHECK(planner.dispatch(Undo{}).ok);
    const Outcome after_undo = planner.dispatch(ShowTree{});
    CHECK(after_undo.message.find("transform=\"translate(3 4)\"") != std::string::npos);
    CHECK(planner.dispatch(Undo{}).ok);
    const Outcome after_both = planner.dispatch(ShowTree{});
    CHECK(after_both.message.find("transform") == std::string::npos);

    // Units: mm and cm are a power of ten apart and add up, in the unit the value was
    // written in, while in and mm do not convert here at all.
    const Outcome ruler = planner.dispatch(CreateRect{"ruler", Length{2.0F, Length::Unit::Cm},
                                                      Length{0.0F, Length::Unit::Mm},
                                                      Length{10.0F, Length::Unit::Mm},
                                                      Length{10.0F, Length::Unit::Mm}, std::nullopt});
    CHECK(ruler.ok);
    CHECK(planner.dispatch(Move{SvgIdRef{"ruler"}, Length{5.0F, Length::Unit::Mm}, Length{0.0F, Length::Unit::Mm}})
              .ok);  // 2cm + 5mm = 2.5cm
    const Outcome converted = planner.dispatch(ShowTree{});
    CHECK(converted.message.find("x=\"2.5cm\"") != std::string::npos);

    const Outcome in_into_cm = planner.dispatch(Move{SvgIdRef{"ruler"}, Length{1.0F, Length::Unit::In},
                                                     Length{0.0F, Length::Unit::Mm}});
    CHECK(!in_into_cm.ok);
    CHECK(in_into_cm.message.find("only mm and cm convert") != std::string::npos);

    // A user unit is not a px: its size is whatever the viewport says, so that pair is
    // refused even though one of the two is a plain number.
    const Outcome px_offset = planner.dispatch(Move{SvgIdRef{"box"}, units(5), units(0)});
    CHECK(px_offset.ok);  // 0 user units + 5 user units
    const Outcome px_into_user_unit =
        planner.dispatch(Move{SvgIdRef{"box"}, Length{1.0F, Length::Unit::Px}, units(0)});
    CHECK(!px_into_user_unit.ok);
    CHECK(px_into_user_unit.message.find("only mm and cm convert") != std::string::npos);

    if (failures != 0) {
        std::printf("pipeline: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("pipeline: all checks passed\n");
    return 0;
}
