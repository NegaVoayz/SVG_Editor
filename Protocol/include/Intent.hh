#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "EditSelectors.hh"
#include "NodeRef.hh"
#include "SVGNode.hh"

// The vocabulary a front end speaks: one structured instruction per thing a user can ask
// for, with model values in it rather than text. "create rect box 0 0 10 10 #ff0000" is a
// Create of a CreateRect by the time it leaves the ui layer, so nothing above that parses a
// string, and nothing below it needs to know how an instruction was written.

// --- the instructions ---

// Every create names the element it makes and where to put it: `parent` is where the
// command's `in <gid>` points, and the root when it said nothing, so a fresh element is
// never a node that nothing can reach.
struct CreateRect {
    std::string id;
    LengthPercentage x, y, width, height;
    std::optional<Color> fill;
    NodeRef parent = RootRef{};
    bool parent_named = false;
};
struct CreateCircle {
    std::string id;
    LengthPercentage cx, cy, r;
    std::optional<Color> fill;
    NodeRef parent = RootRef{};
    bool parent_named = false;
};
struct CreateEllipse {
    std::string id;
    LengthPercentage cx, cy, rx, ry;
    std::optional<Color> fill;
    NodeRef parent = RootRef{};
    bool parent_named = false;
};
// A line has no inside to fill, so what it may carry is a stroke.
struct CreateLine {
    std::string id;
    LengthPercentage x1, y1, x2, y2;
    std::optional<Color> stroke;
    NodeRef parent = RootRef{};
    bool parent_named = false;
};
struct CreatePolygon {
    std::string id;
    std::vector<Shape::Point> points;
    std::optional<Color> fill;
    NodeRef parent = RootRef{};
    bool parent_named = false;
};
// A text element and the character data inside it are two nodes, which is why the content
// travels here as its own string and becomes a child: a <text> holds text the way a group
// holds elements.
struct CreateText {
    std::string id;
    LengthPercentage x, y;
    std::string content;
    NodeRef parent = RootRef{};
    bool parent_named = false;
};
struct CreateGroup {
    std::string id;
    NodeRef parent = RootRef{};
    bool parent_named = false;
};
// A composite create: the group is the node the user names, while the outline and
// label are derived children written with it in the same transaction.
struct CreateBox {
    std::string id;
    LengthPercentage x, y, width, height;
    std::string label;
    std::optional<Color> fill;
    NodeRef parent = RootRef{};
    bool parent_named = false;
};
struct Delete { NodeRef node; };
// The selectors are optional. With neither, a leaf changes its own params and a
// container changes its descendants' params; the Editor refuses the combinations that
// have no meaning for the node it resolved.
struct Move {
    NodeRef node;
    LengthPercentage dx, dy;
    EditTarget target = EditTarget::Default;
    EditScope scope = EditScope::Default;
};
struct ScaleBy {
    NodeRef node;
    float factor;
    EditTarget target = EditTarget::Default;
    EditScope scope = EditScope::Default;
};
// A group comes into being where the first node it takes used to stand, and takes the rest
// of them in the order they were written.
struct Group { std::string id; std::vector<NodeRef> nodes; };
struct Ungroup { NodeRef node; };
struct AddTo { NodeRef parent, node; };
// The child chain is the z-order, last child in front: SVG paints later children on top.
struct Front { NodeRef node; };
struct Back { NodeRef node; };

// --- setting one attribute ---

// `set <id> <attribute> <value>` names its attribute at runtime, but the name decides the
// value's type before the document is consulted. Each attribute is therefore a type of its
// own: the front end reads the word as the type that name implies, and the Editor writes the
// member that type names, with no chain over attribute strings below the ui layer.
//
// The `name` member is the spelling the front end looks up and the word an attribute a node
// does not carry is refused with, so the two cannot drift apart. `id`, the parent links and
// the child list are deliberately absent: a handle is the hive's, and where a node sits is
// the Editor's structural vocabulary (`addto`, `front`, `back`, ...), not an attribute.
struct SetX {
    static constexpr std::string_view name = "x";
    NodeRef node;
    LengthPercentage value;
};
struct SetY {
    static constexpr std::string_view name = "y";
    NodeRef node;
    LengthPercentage value;
};
struct SetW {
    static constexpr std::string_view name = "w";
    NodeRef node;
    LengthPercentage value;
};
struct SetH {
    static constexpr std::string_view name = "h";
    NodeRef node;
    LengthPercentage value;
};
struct SetCx {
    static constexpr std::string_view name = "cx";
    NodeRef node;
    LengthPercentage value;
};
struct SetCy {
    static constexpr std::string_view name = "cy";
    NodeRef node;
    LengthPercentage value;
};
struct SetR {
    static constexpr std::string_view name = "r";
    NodeRef node;
    LengthPercentage value;
};
struct SetRx {
    static constexpr std::string_view name = "rx";
    NodeRef node;
    LengthPercentage value;
};
struct SetRy {
    static constexpr std::string_view name = "ry";
    NodeRef node;
    LengthPercentage value;
};
struct SetX1 {
    static constexpr std::string_view name = "x1";
    NodeRef node;
    LengthPercentage value;
};
struct SetY1 {
    static constexpr std::string_view name = "y1";
    NodeRef node;
    LengthPercentage value;
};
struct SetX2 {
    static constexpr std::string_view name = "x2";
    NodeRef node;
    LengthPercentage value;
};
struct SetY2 {
    static constexpr std::string_view name = "y2";
    NodeRef node;
    LengthPercentage value;
};
struct SetDx {
    static constexpr std::string_view name = "dx";
    NodeRef node;
    LengthPercentage value;
};
struct SetDy {
    static constexpr std::string_view name = "dy";
    NodeRef node;
    LengthPercentage value;
};
struct SetStrokeWidth {
    static constexpr std::string_view name = "stroke-width";
    NodeRef node;
    LengthPercentage value;
};
struct SetWidth {
    static constexpr std::string_view name = "width";
    NodeRef node;
    LengthPercentage value;
};
struct SetHeight {
    static constexpr std::string_view name = "height";
    NodeRef node;
    LengthPercentage value;
};
struct SetStartOffset {
    static constexpr std::string_view name = "startOffset";
    NodeRef node;
    LengthPercentage value;
};
// The namespace a root <svg> declares, and the viewBox that maps its user units onto
// the viewport. Both belong to the SVG container rather than to the document.
struct SetXmlns {
    static constexpr std::string_view name = "xmlns";
    NodeRef node;
    std::string value;
};
struct SetViewBox {
    static constexpr std::string_view name = "viewBox";
    NodeRef node;
    Container::ViewBox value;
};
// Text alignment and size are attributes of a text and of the spans inside it.
struct SetTextAnchor {
    static constexpr std::string_view name = "text-anchor";
    NodeRef node;
    Text::Anchor value;
};
struct SetFontSize {
    static constexpr std::string_view name = "font-size";
    NodeRef node;
    LengthPercentage value;
};
struct SetClass {
    static constexpr std::string_view name = "class";
    NodeRef node;
    std::string value;
};
struct SetFill {
    static constexpr std::string_view name = "fill";
    NodeRef node;
    Paint value;
};
struct SetStroke {
    static constexpr std::string_view name = "stroke";
    NodeRef node;
    Paint value;
};
struct SetOpacity {
    static constexpr std::string_view name = "opacity";
    NodeRef node;
    float value;
};
// `none` clears the transform rather than writing a transform called none, which is the
// one attribute whose absence is a value a user can ask for.
struct SetTransform {
    static constexpr std::string_view name = "transform";
    NodeRef node;
    // The composed matrix. The identity clears the attribute.
    Transform value;
};
struct SetHref {
    static constexpr std::string_view name = "href";
    NodeRef node;
    std::string value;
};
// The words of a text element are its visible content, so `text` is the ordinary name
// for setting them even though SVG stores them in a PlainText child.
struct SetText {
    static constexpr std::string_view name = "text";
    NodeRef node;
    std::string value;
};
// The spelling before `text` existed. It remains an alias for the same edit.
struct SetContent {
    static constexpr std::string_view name = "content";
    NodeRef node;
    std::string value;
};
struct SetPoints {
    static constexpr std::string_view name = "points";
    NodeRef node;
    std::vector<Shape::Point> value;
};

// The attributes, one variant. `set` is one instruction, as `create` is, and which
// attribute it names is the second question - asked where the node that has to carry it is
// known.
using Set = std::variant<SetX, SetY, SetW, SetH, SetCx, SetCy, SetR, SetRx, SetRy, SetX1, SetY1, SetX2, SetY2, SetDx,
                         SetDy, SetStrokeWidth, SetWidth, SetHeight, SetStartOffset, SetXmlns, SetViewBox,
                         SetTextAnchor, SetFontSize, SetClass, SetFill, SetStroke, SetTransform, SetHref, SetText,
                         SetContent, SetPoints, SetOpacity>;

struct Undo {};
struct Redo {};
struct Checkout {
    CommitId commit;
    std::optional<std::size_t> limit = std::nullopt;
};
struct SetCanvas { MaybeLengthPercentage width, height; };
// Back to a fresh document: an empty tree, the canvas the instruction gave (default 800x600
// for whichever side it left out), no history and no file behind it.
struct Init { MaybeLengthPercentage width, height; };
struct ShowTree { NodeRef node = RootRef{}; };
struct Show { NodeRef node; };
struct ShowHistory {
    std::optional<std::size_t> limit;
};
struct Read { std::string path; };
// No path: write where the document was read from, which FileSystem remembers.
struct Save { std::optional<std::string> path; };

// The element instructions, one variant of their own: which element a line asks for is a
// question for the layer that builds one, so the Planner only has to know that a line is a
// create, and the Editor asks the second question when it is the one holding the line.
using Create =
    std::variant<CreateRect, CreateCircle, CreateEllipse, CreateLine, CreatePolygon, CreateText, CreateGroup, CreateBox>;

// One instruction, of exactly one of those types: the front end produces it and both the
// Planner and the Editor dispatch on it with std::visit, so every case is answered by a
// specialization rather than by a chain over verbs. A create is answered by a second visit,
// over the kinds it may hold.
using Intent = std::variant<Create, Delete, Move, ScaleBy, Group, Ungroup, AddTo, Front, Back, Set, Undo, Redo,
                            Checkout, SetCanvas, Init, ShowTree, Show, ShowHistory, Read, Save>;

// The context instructions on their own. FileSystem owns their file syntax, paths and
// context baselines, so the Planner only has to recognise that a request is one of them.
using FileIntent = std::variant<Init, Read, Save>;

// The change instructions on their own: the Editor is typed to these, so a view or a
// context instruction reaching the write path is not a runtime branch it has to refuse, it
// is a type it does not accept.
using ChangeIntent =
    std::variant<Create, Delete, Move, ScaleBy, Group, Ungroup, AddTo, Front, Back, Set, SetCanvas, Undo, Redo,
                 Checkout>;
