#include "Planner.hh"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

// Reporting is the Planner's half of a view: the Model hands out handles and
// payloads, the words around them are chosen here. Every kind of node, container,
// shape and colour is named by an overload of its own, so a kind added to the model
// is a build error here until it is named too.

struct ShapeName {
    std::string_view operator()(const Shape::Rect&) const { return "rect"; }
    std::string_view operator()(const Shape::Circle&) const { return "circle"; }
    std::string_view operator()(const Shape::Ellipse&) const { return "ellipse"; }
    std::string_view operator()(const Shape::Line&) const { return "line"; }
};

struct ContainerName {
    std::string_view operator()(const Container::SVG&) const { return "svg"; }
    std::string_view operator()(const Container::Group&) const { return "g"; }
    std::string_view operator()(const Container::Defs&) const { return "defs"; }
    std::string_view operator()(const Container::Symbol&) const { return "symbol"; }
    std::string_view operator()(const Container::Switch&) const { return "switch"; }
};

struct ElementName {
    std::string_view operator()(const Shape& shape) const { return std::visit(ShapeName{}, shape.specific); }
    std::string_view operator()(const Container& container) const {
        return std::visit(ContainerName{}, container.specific);
    }
    std::string_view operator()(const Text&) const { return "text"; }
};

struct GeometryText {
    std::string operator()(const Shape::Rect& rect) const {
        return " x=\"" + to_text(rect.x) + "\" y=\"" + to_text(rect.y) + "\" w=\"" + to_text(rect.w) +
               "\" h=\"" + to_text(rect.h) + "\"";
    }
    std::string operator()(const Shape::Circle& circle) const {
        return " cx=\"" + to_text(circle.cx) + "\" cy=\"" + to_text(circle.cy) + "\" r=\"" + to_text(circle.r) + "\"";
    }
    std::string operator()(const Shape::Ellipse& ellipse) const {
        return " cx=\"" + to_text(ellipse.cx) + "\" cy=\"" + to_text(ellipse.cy) + "\" rx=\"" +
               to_text(ellipse.rx) + "\" ry=\"" + to_text(ellipse.ry) + "\"";
    }
    std::string operator()(const Shape::Line& line) const {
        return " x1=\"" + to_text(line.x1) + "\" y1=\"" + to_text(line.y1) + "\" x2=\"" + to_text(line.x2) +
               "\" y2=\"" + to_text(line.y2) + "\"";
    }
};

// A shape has coordinates and the two other kinds of node do not, and this says so
// instead of asking the variant which one it is holding.
struct GeometryTextOf {
    std::string operator()(const Shape& shape) const { return std::visit(GeometryText{}, shape.specific); }
    std::string operator()(const Container&) const { return {}; }
    std::string operator()(const Text&) const { return {}; }
};

std::string hex_text(uint8_t value) {
    constexpr char digits[] = "0123456789abcdef";
    return std::string{digits[value / 16], digits[value % 16]};
}

// The colours that need a table this does not have - a system colour, or a named
// one, which the model has no ids for - read as nothing.
struct ColorText {
    std::string operator()(const Color::None&) const { return "none"; }
    std::string operator()(const Color::Current&) const { return "currentColor"; }
    std::string operator()(const Color::System&) const { return {}; }
    std::string operator()(const Color::Named&) const { return {}; }
    std::string operator()(const Color::RGB& rgb) const {
        return "#" + hex_text(rgb.r) + hex_text(rgb.g) + hex_text(rgb.b);
    }
    std::string operator()(const Color::FuncRGB& rgb) const {
        return "rgb(" + std::to_string(rgb.r) + "," + std::to_string(rgb.g) + "," + std::to_string(rgb.b) + ")";
    }
};

struct PaintText {
    std::string operator()(const Color& color) const { return std::visit(ColorText{}, color.specific); }
    std::string operator()(const PaintServer&) const { return {}; }  // a paint server needs a lookup by url
};

// The transform a node carries, in the form it is written in.
struct TransformText {
    std::string operator()(const Translate& translate) const {
        return "translate(" + to_text(translate.x) + " " + to_text(translate.y) + ")";
    }
    std::string operator()(const Scale& scale) const {
        return "scale(" + number_text(scale.x) + " " + number_text(scale.y) + ")";
    }
    std::string operator()(const Rotate& rotate) const {
        return "rotate(" + number_text(rotate.degrees) + ")";
    }
    std::string operator()(const Matrix& matrix) const {
        return "matrix(" + number_text(matrix.a) + " " + number_text(matrix.b) + " " + number_text(matrix.c) + " " +
               number_text(matrix.d) + " " + number_text(matrix.e) + " " + number_text(matrix.f) + ")";
    }
};

struct NodeText {
    std::string operator()(const PlainText&) const { return "<text-node>"; }
    std::string operator()(const SVGNode& element) const {
        std::string text = "<" + std::string{std::visit(ElementName{}, element.data)} + ">";
        if (!element.identity_attr.id.empty()) text += " id=\"" + element.identity_attr.id + "\"";
        text += std::visit(GeometryTextOf{}, element.data);
        if (element.transform_attr.transform) {
            text += " transform=\"" + std::visit(TransformText{}, *element.transform_attr.transform) + "\"";
        }
        if (element.paint_attr.fill) {
            const std::string fill = std::visit(PaintText{}, *element.paint_attr.fill);
            if (!fill.empty()) text += " fill=\"" + fill + "\"";
        }
        return text;
    }
};

std::string describe(const Node& payload) { return std::visit(NodeText{}, payload); }

void append_tree(std::string& out, const Document& document, Id node, std::size_t depth) {
    const std::optional<Node> payload = document.node(node);
    if (!payload) return;

    out.append(depth * 2, ' ');
    out += "#" + std::to_string(node) + " " + describe(*payload) + "\n";
    for (const Id child : document.children(node)) append_tree(out, document, child, depth + 1);
}

}  // namespace

Planner::Planner(Document& document, Editor& editor) : document_(document), editor_(editor) {}

// The routes, one explicit specialization per instruction: the primary template in
// the header is the only thing an instruction with no route can reach, and it fails
// the build naming the type.
template<> Outcome Planner::handle<ShowTree>(const ShowTree&) { return show_tree(); }
template<> Outcome Planner::handle<Find>(const Find& intent) { return show_find(intent); }

template<> Outcome Planner::handle<CreateRect>(const CreateRect& intent) {
    return editor_.translate(ChangeIntent{intent});
}
template<> Outcome Planner::handle<CreateGroup>(const CreateGroup& intent) {
    return editor_.translate(ChangeIntent{intent});
}
template<> Outcome Planner::handle<Delete>(const Delete& intent) { return editor_.translate(ChangeIntent{intent}); }
template<> Outcome Planner::handle<Move>(const Move& intent) { return editor_.translate(ChangeIntent{intent}); }
template<> Outcome Planner::handle<AddTo>(const AddTo& intent) { return editor_.translate(ChangeIntent{intent}); }
template<> Outcome Planner::handle<Undo>(const Undo&) { return editor_.translate(ChangeIntent{Undo{}}); }
template<> Outcome Planner::handle<Redo>(const Redo&) { return editor_.translate(ChangeIntent{Redo{}}); }

template<> Outcome Planner::handle<Open>(const Open& intent) { return open(intent); }
template<> Outcome Planner::handle<Save>(const Save& intent) { return save(intent); }

Outcome Planner::dispatch(const Intent& intent) {
    return std::visit([this](const auto& value) { return handle(value); }, intent);
}

Outcome Planner::show_tree() const {
    std::string out;
    append_tree(out, document_, document_.root(), 0);
    if (!out.empty()) out.pop_back();  // the last line does not need the newline
    return Outcome{true, Kind::View, std::move(out)};
}

Outcome Planner::show_find(const Find& intent) const {
    const std::optional<Id> found = document_.find(intent.svg_id);
    if (!found) return Outcome{false, Kind::View, "no node has the id \"" + intent.svg_id + "\""};

    const std::optional<Node> payload = document_.node(*found);
    if (!payload) return Outcome{false, Kind::View, "the node went missing"};  // cannot happen: find()
    return Outcome{true, Kind::View, "#" + std::to_string(*found) + " " + describe(*payload)};
}

Outcome Planner::open(const Open& intent) const {
    const Status status = document_.open(intent.path);
    return Outcome{status.ok, Kind::Context, status.message};
}

Outcome Planner::save(const Save& intent) const {
    const Status status = document_.save(intent.path);
    return Outcome{status.ok, Kind::Context, status.message};
}
