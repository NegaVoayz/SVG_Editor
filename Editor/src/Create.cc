// Building an element. Six kinds share one body, because what differs between them is
// only what they bring: the element itself, the tag a message names it by, and the words
// to put inside it when the element is a text.
//
// The second level of the dispatch lives here too: `handle<Create>` is the question of which
// element a create holds, asked where the builders it may answer with are.
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "NodeRef.hh"
#include "Outcome.hh"
#include "Translations.hh"

namespace {

// One element with an id and a body: every kind builds its own body and comes here for
// the two fields they all share.
Node element(std::string svg_id, std::variant<Shape, Container, Text> body) {
    SVGNode node;
    node.identity_attr = IdentityAttr{std::move(svg_id), ""};
    node.data = std::move(body);
    return Node{std::move(node)};
}

// The character data inside a text element. It is a node of its own, which is why a text
// element is written in two steps and why its words stay where SVG keeps them - inside
// the element, rather than in a string field of it.
Node characters(std::string content) { return Node{PlainText{std::move(content)}}; }

void fill(Node& payload, const std::optional<Color>& color) {
    if (!color) return;
    std::get<SVGNode>(payload).paint_attr.fill = Paint{*color};
}

// A line has no inside to fill, so the one colour it takes is its stroke.
void stroke(Node& payload, const std::optional<Color>& color) {
    if (!color) return;
    std::get<SVGNode>(payload).paint_attr.stroke = Paint{*color};
}

struct ShapeKind {
    std::string_view operator()(const Shape::Rect&) const { return "rect"; }
    std::string_view operator()(const Shape::Circle&) const { return "circle"; }
    std::string_view operator()(const Shape::Ellipse&) const { return "ellipse"; }
    std::string_view operator()(const Shape::Line&) const { return "line"; }
};

struct ContainerKind {
    std::string_view operator()(const Container::SVG&) const { return "svg"; }
    std::string_view operator()(const Container::Group&) const { return "g"; }
    std::string_view operator()(const Container::Defs&) const { return "defs"; }
    std::string_view operator()(const Container::Symbol&) const { return "symbol"; }
    std::string_view operator()(const Container::Switch&) const { return "switch"; }
};

struct ElementKind {
    std::string_view operator()(const Shape& shape) const { return std::visit(ShapeKind{}, shape.specific); }
    std::string_view operator()(const Container& container) const {
        return std::visit(ContainerKind{}, container.specific);
    }
    std::string_view operator()(const Text&) const { return "text"; }
};

struct ElementIdentity {
    std::string_view element;
    std::string_view svg_id;
};

// The identity a create event reports for a node: its SVG tag and its own id, with the
// empty id of the root standing for no name. The visitor is exhaustive, so a new kind of
// element cannot reach an event without a name.
std::optional<ElementIdentity> identity_of(const Node& payload) {
    if (!std::holds_alternative<SVGNode>(payload)) return std::nullopt;
    const SVGNode& node = std::get<SVGNode>(payload);
    const std::string_view element = std::visit(ElementKind{}, node.data);
    return ElementIdentity{element, node.identity_attr.id};
}

}  // namespace

// The one element that is built for an edit rather than for a command: `group` takes
// nodes that already exist and needs somewhere to put them.
Node make_group(std::string svg_id) {
    return element(std::move(svg_id), Container{ChildAttr{}, Container::Group{}});
}

Editor::NewElement Editor::new_element(const CreateGroup& intent) const {
    return NewElement{"g", make_group(intent.id), std::nullopt};
}

Editor::NewElement Editor::new_element(const CreateRect& intent) const {
    Node payload = element(intent.id, Shape{Shape::Rect{intent.x, intent.y, intent.width, intent.height}});
    fill(payload, intent.fill);
    return NewElement{"rect", std::move(payload), std::nullopt};
}

Editor::NewElement Editor::new_element(const CreateCircle& intent) const {
    Node payload = element(intent.id, Shape{Shape::Circle{intent.cx, intent.cy, intent.r}});
    fill(payload, intent.fill);
    return NewElement{"circle", std::move(payload), std::nullopt};
}

Editor::NewElement Editor::new_element(const CreateEllipse& intent) const {
    Node payload = element(intent.id, Shape{Shape::Ellipse{intent.cx, intent.cy, intent.rx, intent.ry}});
    fill(payload, intent.fill);
    return NewElement{"ellipse", std::move(payload), std::nullopt};
}

Editor::NewElement Editor::new_element(const CreateLine& intent) const {
    Node payload = element(intent.id, Shape{Shape::Line{intent.x1, intent.y1, intent.x2, intent.y2}});
    stroke(payload, intent.stroke);
    return NewElement{"line", std::move(payload), std::nullopt};
}

Editor::NewElement Editor::new_element(const CreateText& intent) const {
    Text text;
    text.common.x = intent.x;
    text.common.y = intent.y;
    text.child_attr = ChildAttr{};  // an empty child list, filled when children arrive
    return NewElement{"text", element(intent.id, std::move(text)), intent.content};
}

template<typename T>
Outcome Editor::create(const T& intent) {
    NewElement built = new_element(intent);
    if (document_.find(intent.id)) {
        return refused(Kind::Change, fault::DuplicateId{intent.id});
    }
    const std::expected<Id, Refusal> parent = resolve_container(intent.parent);
    if (!parent) return refused(Kind::Change, parent.error());
    const std::optional<Node> parent_payload = document_.node(*parent);
    if (!parent_payload) return refused(Kind::Change, fault::InternalError{});
    const std::optional<ElementIdentity> parent_identity = identity_of(*parent_payload);
    if (!parent_identity) return refused(Kind::Change, fault::InternalError{});

    // The element and its content are two nodes, and one instruction is one
    // transaction, so either both are written or neither is.
    Transaction edit = document_.transaction();
    const Id node = edit.append_child(*parent, std::move(built.payload));
    if (built.content) edit.append_child(node, characters(std::move(*built.content)));
    edit.commit();
    const bool parent_named = intent.parent_named || !std::holds_alternative<RootRef>(intent.parent);
    return happened(Kind::Change,
                    event::Created{node, std::string{built.kind}, intent.id, *parent,
                                   std::string{parent_identity->element}, std::string{parent_identity->svg_id},
                                   parent_named});
}

// The second level of the dispatch: a create is one instruction, and which element it builds
// is the next question. Each kind comes back to handle(), where the builder it has is what
// answers it. create() is defined in this file, so the visit below is also what instantiates
// it for every kind - the kinds are the alternatives of Create, and nothing is listed twice.
template<> Outcome Editor::handle<Create>(const Create& intent) {
    return std::visit([this](const auto& element) { return handle(element); }, intent);
}
