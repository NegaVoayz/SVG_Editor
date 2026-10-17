#include "Rendering.hh"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "SVGNode.hh"

namespace {

// --- what an element is called ---

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

// Every kind of node, container, shape and text has an overload here, so a kind added to
// the model is a build error in this file until it is named too.
struct ElementName {
    std::string_view operator()(const Shape& shape) const { return std::visit(ShapeName{}, shape.specific); }
    std::string_view operator()(const Container& container) const {
        return std::visit(ContainerName{}, container.specific);
    }
    std::string_view operator()(const Text&) const { return "text"; }
};

// --- what an element carries ---

// A shape's own values, in the same words `create` takes them in, so a line of the tree
// reads like the instruction that made it.
struct GeometryText {
    std::string operator()(const Shape::Rect& rect) const {
        return "x=" + to_text(rect.x) + " y=" + to_text(rect.y) + " w=" + to_text(rect.w) + " h=" + to_text(rect.h);
    }
    std::string operator()(const Shape::Circle& circle) const {
        return "cx=" + to_text(circle.cx) + " cy=" + to_text(circle.cy) + " r=" + to_text(circle.r);
    }
    std::string operator()(const Shape::Ellipse& ellipse) const {
        return "cx=" + to_text(ellipse.cx) + " cy=" + to_text(ellipse.cy) + " rx=" + to_text(ellipse.rx) +
               " ry=" + to_text(ellipse.ry);
    }
    std::string operator()(const Shape::Line& line) const {
        return "x1=" + to_text(line.x1) + " y1=" + to_text(line.y1) + " x2=" + to_text(line.x2) +
               " y2=" + to_text(line.y2);
    }
};

// The canvas of a document, as one value: the two sides are joined by an x, the way a
// user writes a size, and a unit the two of them share is said once.
std::string canvas_text(const Container::SVG& canvas) {
    if (canvas.width && canvas.height && std::holds_alternative<Length>(*canvas.width) &&
        std::holds_alternative<Length>(*canvas.height)) {
        const Length& width = std::get<Length>(*canvas.width);
        const Length& height = std::get<Length>(*canvas.height);
        if (width.unit == height.unit) {
            return "(" + number_text(width.value) + "x" + number_text(height.value) + ")" +
                   std::string{unit_text(width.unit)};
        }
    }
    const std::string width = canvas.width ? to_text(*canvas.width) : "?";
    const std::string height = canvas.height ? to_text(*canvas.height) : "?";
    return "(" + width + "x" + height + ")";
}

// A container carries nothing of its own, except the root, whose size is the canvas.
struct ContainerText {
    std::string operator()(const Container::SVG& canvas) const { return canvas_text(canvas); }
    std::string operator()(const Container::Group&) const { return {}; }
    std::string operator()(const Container::Defs&) const { return {}; }
    std::string operator()(const Container::Symbol&) const { return {}; }
    std::string operator()(const Container::Switch&) const { return {}; }
};

// A text's position reads as a point: SVG writes its two values apart, and that is what
// they are together.
std::string position_text(const Text& text) {
    if (!text.common.x) return {};
    if (!text.common.y) return "x=" + to_text(*text.common.x);
    return "(" + to_text(*text.common.x) + "," + to_text(*text.common.y) + ")";
}

struct AttrsText {
    std::string operator()(const Shape& shape) const { return std::visit(GeometryText{}, shape.specific); }
    std::string operator()(const Container& container) const { return std::visit(ContainerText{}, container.specific); }
    std::string operator()(const Text& text) const { return position_text(text); }
};

std::string hex_text(uint8_t value) {
    constexpr char digits[] = "0123456789abcdef";
    return std::string{digits[value / 16], digits[value % 16]};
}

// The colours that need a table this does not have - a system colour, or a named one,
// which the model has no ids for - read as nothing.
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

// --- one line's worth of a node ---

// The two halves of a line, kept apart because the tree lines the second one up in a
// column of its own, and because a node may carry nothing at all.
struct Words {
    std::string name;
    std::string attrs;
};

void append_word(Words& words, std::string word) {
    if (word.empty()) return;
    if (!words.attrs.empty()) words.attrs += " ";
    words.attrs += std::move(word);
}

Words element_words(const SVGNode& element, const std::string& text) {
    Words words;
    const std::string_view kind = std::visit(ElementName{}, element.data);
    // The kind and the id the user wrote: the handle is not part of the drawing, because
    // it is the hive's virtual id, which is never reused for another node.
    words.name =
        element.identity_attr.id.empty() ? std::string{kind} : std::string{kind} + "#" + element.identity_attr.id;

    append_word(words, std::visit(AttrsText{}, element.data));

    // A text's words are the character data inside it. That is a node of the model and it
    // is not a line of the drawing: printing it inside the element is what makes a text
    // read as a text.
    if (std::holds_alternative<Text>(element.data)) append_word(words, "\"" + text + "\"");

    if (element.transform_attr.transform) {
        append_word(words, "transform=" + std::visit(TransformText{}, *element.transform_attr.transform));
    }
    if (element.paint_attr.fill) {
        const std::string paint = std::visit(PaintText{}, *element.paint_attr.fill);
        if (!paint.empty()) append_word(words, "fill=" + paint);
    }
    if (element.paint_attr.stroke) {
        const std::string paint = std::visit(PaintText{}, *element.paint_attr.stroke);
        if (!paint.empty()) append_word(words, "stroke=" + paint);
    }
    if (element.paint_attr.stroke_width) {
        append_word(words, "stroke-width=" + to_text(*element.paint_attr.stroke_width));
    }
    return words;
}

Words words_of(const Node& payload, const std::string& text) {
    if (std::holds_alternative<SVGNode>(payload)) return element_words(std::get<SVGNode>(payload), text);

    // Character data reaches a view on its own only when something asked for it by
    // handle: it has no id to be named by.
    return Words{"<text-node>", "\"" + text + "\""};
}

// One line of a tree, before the column is known: where it hangs, and what it says.
struct Line {
    std::string prefix;
    std::string name;
    std::string attrs;
};

// How wide a line is where it is read. The box drawing is what makes this a function:
// every character of it is one place and three bytes, so counting bytes would line the
// column up two places too far for every bar in front of it.
std::size_t width_of(std::string_view text) {
    std::size_t width = 0;
    for (const char piece : text) {
        // a continuation byte is part of the character before it, not a place of its own
        if ((static_cast<unsigned char>(piece) & 0xC0) != 0x80) ++width;
    }
    return width;
}

}  // namespace

std::string render(const TreeView& view) {
    std::vector<Line> lines;

    // The rows are a walk of the tree, so the ancestors of a row are the rows above it at
    // the depths it does not sit at: a bar is drawn for each of them that had a sibling
    // after it, which is the whole of what the drawing says.
    std::vector<bool> followed(1, false);
    for (std::size_t index = 0; index < view.rows.size(); ++index) {
        const TreeRow& row = view.rows[index];
        const Words words = words_of(row.payload, row.text);

        std::string prefix;
        if (index > 0) {  // the first row is what the view is rooted at, and it hangs from nothing
            followed.resize(row.depth + 1, false);
            followed[row.depth] = !row.last;
            for (std::size_t level = 1; level < row.depth; ++level) prefix += followed[level] ? "│   " : "    ";
            prefix += row.last ? "└── " : "├── ";
        }
        lines.push_back(Line{std::move(prefix), words.name, words.attrs});
    }

    // Everything under the first line lines its attributes up in one column, two places
    // past the longest name in the view. The box drawing counts as part of that name,
    // which is why a deeper line starts further right and the column is still one column.
    std::size_t column = 0;
    for (std::size_t index = 1; index < lines.size(); ++index) {
        column = std::max(column, width_of(lines[index].prefix) + lines[index].name.size() + 2);
    }

    std::string out;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const Line& line = lines[index];
        if (index > 0) out += "\n";
        out += line.prefix + line.name;
        if (!line.attrs.empty()) {
            // The first line is not padded: it is what everything else hangs from, so
            // there is no column in front of it to line up with.
            out.append(index > 0 ? column - (width_of(line.prefix) + line.name.size()) : 1, ' ');
            out += line.attrs;
        }
    }
    return out;
}

std::string render(const NodeView& view) {
    const Words words = words_of(view.payload, view.text);
    return words.attrs.empty() ? words.name : words.name + " " + words.attrs;
}

std::string render(const View& view) {
    if (std::holds_alternative<TreeView>(view)) return render(std::get<TreeView>(view));
    if (std::holds_alternative<NodeView>(view)) return render(std::get<NodeView>(view));
    return {};
}

