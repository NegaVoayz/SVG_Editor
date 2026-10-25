// Building an element. Eight kinds share one body, because what differs between them is
// only the detached subtree they bring: one element, text character data, or a composite
// tree whose nodes are written together.
//
// The second level of the dispatch lives here too: `handle<Create>` is the question of which
// element a create holds, asked where the builders it may answer with are.
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>

#include "Naming.hh"
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

std::optional<Refusal> finite_length(std::string_view attribute, LengthPercentage value) {
    if (!is_finite(value)) return fault::NonFiniteValue{std::string{attribute}};
    return std::nullopt;
}

std::optional<Refusal> nonnegative(std::string_view attribute, LengthPercentage value) {
    if (std::optional<Refusal> problem = finite_length(attribute, value)) return problem;
    if (numeric_value(value) < 0.0F) return fault::NegativeDimension{std::string{attribute}};
    return std::nullopt;
}

std::optional<Refusal> dimension_problem(const CreateRect& intent) {
    if (std::optional<Refusal> problem = finite_length("x", intent.x)) return problem;
    if (std::optional<Refusal> problem = finite_length("y", intent.y)) return problem;
    if (std::optional<Refusal> problem = nonnegative("width", intent.width)) return problem;
    return nonnegative("height", intent.height);
}

std::optional<Refusal> dimension_problem(const CreateCircle& intent) {
    if (std::optional<Refusal> problem = finite_length("cx", intent.cx)) return problem;
    if (std::optional<Refusal> problem = finite_length("cy", intent.cy)) return problem;
    return nonnegative("r", intent.r);
}

std::optional<Refusal> dimension_problem(const CreateEllipse& intent) {
    if (std::optional<Refusal> problem = finite_length("cx", intent.cx)) return problem;
    if (std::optional<Refusal> problem = finite_length("cy", intent.cy)) return problem;
    if (std::optional<Refusal> problem = nonnegative("rx", intent.rx)) return problem;
    return nonnegative("ry", intent.ry);
}

std::optional<Refusal> dimension_problem(const CreateLine& intent) {
    if (std::optional<Refusal> problem = finite_length("x1", intent.x1)) return problem;
    if (std::optional<Refusal> problem = finite_length("y1", intent.y1)) return problem;
    if (std::optional<Refusal> problem = finite_length("x2", intent.x2)) return problem;
    return finite_length("y2", intent.y2);
}

std::optional<Refusal> dimension_problem(const CreatePolygon& intent) {
    if (intent.points.size() < 3) return fault::InvalidPoints{};
    for (const Shape::Point& point : intent.points) {
        if (std::optional<Refusal> problem = finite_length("x", point.x)) return problem;
        if (std::optional<Refusal> problem = finite_length("y", point.y)) return problem;
    }
    return std::nullopt;
}

std::optional<Refusal> dimension_problem(const CreateText& intent) {
    if (std::optional<Refusal> problem = finite_length("x", intent.x)) return problem;
    return finite_length("y", intent.y);
}

std::optional<Refusal> dimension_problem(const CreateGroup&) { return std::nullopt; }

std::optional<Refusal> dimension_problem(const CreateBox& intent) {
    if (std::optional<Refusal> problem = finite_length("x", intent.x)) return problem;
    if (std::optional<Refusal> problem = finite_length("y", intent.y)) return problem;
    if (std::optional<Refusal> problem = nonnegative("width", intent.width)) return problem;
    return nonnegative("height", intent.height);
}

std::optional<Refusal> subtree_id_problem(const Document& document, const Subtree& subtree,
                                          std::unordered_set<std::string>& seen) {
    if (std::holds_alternative<SVGNode>(subtree.payload)) {
        const SVGNode& element = std::get<SVGNode>(subtree.payload);
        const std::string& id = element.identity_attr.id;
        if (!valid_svg_id(id)) return fault::InvalidId{id};
        if (reserved_svg_id(id)) return fault::ReservedId{id};
        if (document.find(id) || !seen.insert(id).second) return fault::DuplicateId{id};
    }
    for (const Subtree& child : subtree.children) {
        if (std::optional<Refusal> problem = subtree_id_problem(document, child, seen)) return problem;
    }
    return std::nullopt;
}

}  // namespace

// The one element that is built for an edit rather than for a command: `group` takes
// nodes that already exist and needs somewhere to put them.
Node make_group(std::string svg_id) {
    return element(std::move(svg_id), Container{ChildAttr{}, Container::Group{}});
}

Subtree Editor::new_element(const CreateGroup& intent) const {
    return Subtree{make_group(intent.id), {}};
}

Subtree Editor::new_element(const CreateRect& intent) const {
    Node payload = element(intent.id, Shape{Shape::Rect{intent.x, intent.y, intent.width, intent.height}});
    fill(payload, intent.fill);
    return Subtree{std::move(payload), {}};
}

Subtree Editor::new_element(const CreateCircle& intent) const {
    Node payload = element(intent.id, Shape{Shape::Circle{intent.cx, intent.cy, intent.r}});
    fill(payload, intent.fill);
    return Subtree{std::move(payload), {}};
}

Subtree Editor::new_element(const CreateEllipse& intent) const {
    Node payload = element(intent.id, Shape{Shape::Ellipse{intent.cx, intent.cy, intent.rx, intent.ry}});
    fill(payload, intent.fill);
    return Subtree{std::move(payload), {}};
}

Subtree Editor::new_element(const CreateLine& intent) const {
    Node payload = element(intent.id, Shape{Shape::Line{intent.x1, intent.y1, intent.x2, intent.y2}});
    stroke(payload, intent.stroke);
    return Subtree{std::move(payload), {}};
}

Subtree Editor::new_element(const CreatePolygon& intent) const {
    Node payload = element(intent.id, Shape{Shape::Polygon{intent.points}});
    fill(payload, intent.fill);
    return Subtree{std::move(payload), {}};
}

Subtree Editor::new_element(const CreateText& intent) const {
    Text text;
    text.common.x = maybe_length_percentage(intent.x);
    text.common.y = maybe_length_percentage(intent.y);
    text.child_attr = ChildAttr{};  // an empty child list, filled when children arrive
    return Subtree{element(intent.id, std::move(text)),
                   {Subtree{characters(intent.content), {}}}};
}

Subtree Editor::new_element(const CreateBox& intent) const {
    Node group = element(intent.id, Container{ChildAttr{}, Container::Group{std::string{"box"}}});
    Node outline =
        element(intent.id + "-rect", Shape{Shape::Rect{intent.x, intent.y, intent.width, intent.height}});
    fill(outline, intent.fill);

    Text label;
    label.common.x = maybe_length_percentage(intent.x);
    label.common.y = maybe_length_percentage(intent.y);
    label.child_attr = ChildAttr{};
    Subtree label_node{element(intent.id + "-label", std::move(label)),
                       {Subtree{characters(intent.label), {}}}};

    return Subtree{std::move(group),
                   {Subtree{std::move(outline), {}}, std::move(label_node)}};
}

template<typename T>
Outcome Editor::create(const T& intent) {
    std::optional<Refusal> problem = dimension_problem(intent);
    if (problem) return refused(Kind::Change, std::move(*problem));

    Subtree built = new_element(intent);
    std::unordered_set<std::string> ids;
    if (std::optional<Refusal> id_problem = subtree_id_problem(document_, built, ids)) {
        return refused(Kind::Change, std::move(*id_problem));
    }
    const std::expected<Id, Refusal> parent = resolve_container(intent.parent);
    if (!parent) return refused(Kind::Change, parent.error());

    // Whatever the kind brought is one instruction, so the Model writes the whole tree in
    // one transaction and one undo takes it back.
    Transaction edit = document_.transaction();
    const Id node = edit.append_subtree(*parent, std::move(built));
    commit_edit(edit);
    const bool parent_named = intent.parent_named || !std::holds_alternative<RootRef>(intent.parent);
    return happened(Kind::Change,
                    event::Created{name_of(document_, node), name_of(document_, *parent), parent_named});
}

// The second level of the dispatch: a create is one instruction, and which element it builds
// is the next question. Each kind comes back to handle(), where the builder it has is what
// answers it. create() is defined in this file, so the visit below is also what instantiates
// it for every kind - the kinds are the alternatives of Create, and nothing is listed twice.
template<> Outcome Editor::handle<Create>(const Create& intent) {
    return std::visit([this](const auto& element) { return handle(element); }, intent);
}
