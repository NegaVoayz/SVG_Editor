#include "Rendering.hh"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "ElementName.hh"
#include "Exhaustive.hh"
#include "Quoting.hh"
#include "SVGNode.hh"

namespace {

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
    std::string operator()(const Shape::Polygon& polygon) const {
        return "points=\"" + points_text(polygon.points, false) + "\"";
    }
    template <class T>
    std::string operator()(const T&) const {
        static_assert(always_false<T>, "no text for this shape alternative");
        return {};
    }
};

std::string canvas_unit_suffix(Length::Unit unit) {
    if (unit == Length::Unit::UserUnit) return " user units";
    return " " + std::string{unit_text(unit)};
}

std::string canvas_side_text(const MaybeLengthPercentage& value) {
    const std::optional<LengthPercentage> present = present_length_percentage(value);
    if (!present) return "unset";
    if (std::holds_alternative<Length>(*present)) {
        const Length& length = std::get<Length>(*present);
        if (length.unit == Length::Unit::UserUnit) return number_text(length.value) + " user units";
    }
    return to_text(*present);
}

// The canvas of a document, as one value. A unit shared by both sides is inside the
// parentheses, next to the numbers it describes; a partial canvas still names the unit of
// the side that is present.
std::string canvas_text(const Container::SVG& canvas) {
    const std::optional<LengthPercentage> width_value = present_length_percentage(canvas.width);
    const std::optional<LengthPercentage> height_value = present_length_percentage(canvas.height);
    if (!width_value && !height_value) return "(unsized)";
    if (width_value && height_value && std::holds_alternative<Length>(*width_value) &&
        std::holds_alternative<Length>(*height_value)) {
        const Length& width = std::get<Length>(*width_value);
        const Length& height = std::get<Length>(*height_value);
        if (width.unit == height.unit) {
            return "(" + number_text(width.value) + " x " + number_text(height.value) +
                   canvas_unit_suffix(width.unit) + ")";
        }
    }
    return "(" + canvas_side_text(canvas.width) + " x " + canvas_side_text(canvas.height) + ")";
}

// The size an `init` created, without the tree's parentheses. Both sides are always
// present because the Planner reads them back from the new root.
std::string canvas_size_text(const MaybeLengthPercentage& width_value,
                             const MaybeLengthPercentage& height_value) {
    const std::optional<LengthPercentage> width = present_length_percentage(width_value);
    const std::optional<LengthPercentage> height = present_length_percentage(height_value);
    if (width && height && std::holds_alternative<Length>(*width) &&
        std::holds_alternative<Length>(*height)) {
        const Length& width_length = std::get<Length>(*width);
        const Length& height_length = std::get<Length>(*height);
        if (width_length.unit == height_length.unit) {
            return number_text(width_length.value) + "x" + number_text(height_length.value) +
                   std::string{unit_text(width_length.unit)};
        }
    }
    return (width ? to_text(*width) : std::string{"unset"}) + " x " +
           (height ? to_text(*height) : std::string{"unset"});
}

// A container carries nothing of its own, except the root, whose size is the canvas.
struct ContainerText {
    std::string operator()(const Container::SVG& canvas) const {
        std::string out;
        if (!canvas.xmlns.empty() && canvas.xmlns != Container::SVG::default_xmlns) {
            out += "xmlns=\"" + canvas.xmlns + "\" ";
        }
        out += canvas_text(canvas);
        if (canvas.view_box) out += " viewBox=\"" + to_text(*canvas.view_box) + "\"";
        return out;
    }
    std::string operator()(const Container::Group&) const { return {}; }
    std::string operator()(const Container::Defs&) const { return {}; }
    std::string operator()(const Container::Symbol&) const { return {}; }
    std::string operator()(const Container::Switch&) const { return {}; }
    template <class T>
    std::string operator()(const T&) const {
        static_assert(always_false<T>, "no text for this container alternative");
        return {};
    }
};

// A text's position reads as a point: SVG writes its two values apart, and that is what
// they are together.
std::string position_text(const TextShape& text) {
    const std::optional<LengthPercentage> x = present_length_percentage(text.common.x);
    const std::optional<LengthPercentage> y = present_length_percentage(text.common.y);
    if (!x) return {};
    if (!y) return "x=" + to_text(*x);
    return "(" + to_text(*x) + "," + to_text(*y) + ")";
}

std::string text_details(const TextShape& text) {
    std::string out = position_text(text);
    if (text.common.text_anchor) {
        if (!out.empty()) out += ' ';
        out += "text-anchor=";
        out += text_anchor_text(*text.common.text_anchor);
    }
    if (const std::optional<LengthPercentage> font_size =
            present_length_percentage(text.common.font_size)) {
        if (!out.empty()) out += ' ';
        out += "font-size=" + to_text(*font_size);
    }
    return out;
}

struct AttrsText {
    std::string operator()(const Shape& shape) const { return std::visit(GeometryText{}, shape.specific); }
    std::string operator()(const ContainerShape& container) const {
        return std::visit(ContainerText{}, container.specific);
    }
    std::string operator()(const TextShape& text) const { return text_details(text); }
    template <class T>
    std::string operator()(const T&) const {
        static_assert(always_false<T>, "no text for this element payload");
        return {};
    }
};

template<class T>
std::string_view element_data_name(const T&) {
    static_assert(always_false<T>, "no element name for this view payload");
    return {};
}

template<>
std::string_view element_data_name<Shape>(const Shape& payload) {
    return std::visit([](const auto& specific) { return element_name(specific); }, payload.specific);
}

template<>
std::string_view element_data_name<ContainerShape>(const ContainerShape& payload) {
    return std::visit([](const auto& specific) { return element_name(specific); }, payload.specific);
}

template<>
std::string_view element_data_name<TextShape>(const TextShape& payload) {
    return std::visit([](const auto& specific) { return element_name(specific); }, payload.specific);
}

// --- one line's worth of a node ---

// The two halves of a line, kept apart because the tree lines the second one up in a
// column of its own, and because a node may carry nothing at all. A single node view
// prints the details one per line, while the tree joins them with spaces.
struct Words {
    std::string name;
    std::vector<std::string> details;
};

void append_detail(Words& words, std::string detail) {
    if (detail.empty()) return;
    words.details.push_back(std::move(detail));
}

std::string joined_details(const Words& words) {
    std::string out;
    for (const std::string& detail : words.details) {
        if (!out.empty()) out += " ";
        out += detail;
    }
    return out;
}

Words element_words(const ElementShape& element, const Transform& transform, const std::string& text) {
    Words words;
    const std::string_view kind = std::visit(
        [](const auto& payload) {
            using T = std::remove_cvref_t<decltype(payload)>;
            return element_data_name<T>(payload);
        },
        element.data);
    // The kind and the id the user wrote: the handle is not part of the drawing, because
    // it is the hive's virtual id, which is never reused for another node.
    words.name = element.identity_attr.id.empty()
                     ? std::string{kind}
                     : std::string{kind} + "#" + quote_if_needed(element.identity_attr.id);
    if (std::holds_alternative<ContainerShape>(element.data)) {
        const ContainerShape& container = std::get<ContainerShape>(element.data);
        if (std::holds_alternative<Container::Group>(container.specific)) {
            const std::string& data_kind = std::get<Container::Group>(container.specific).data_kind;
            if (!data_kind.empty()) words.name += " (" + data_kind + ")";
        }
    }

    // Details follow the order SvgWriter writes an element: identity, class, data, paint,
    // transform, then the character data inside it. `class` is a list of names, so it is
    // quoted whenever it has more than one.
    if (!element.identity_attr.class_.empty()) {
        append_detail(words, "class=" + quote_if_needed(element.identity_attr.class_));
    }

    // `data` follows class, as the geometry of a shape does after its id and class.
    append_detail(words, std::visit(AttrsText{}, element.data));

    if (element.paint_attr.fill) {
        const std::string paint = to_text(*element.paint_attr.fill);
        if (!paint.empty()) append_detail(words, "fill=" + paint);
    }
    if (element.paint_attr.stroke) {
        const std::string paint = to_text(*element.paint_attr.stroke);
        if (!paint.empty()) append_detail(words, "stroke=" + paint);
    }
    if (const std::optional<LengthPercentage> stroke_width =
            present_length_percentage(element.paint_attr.stroke_width)) {
        append_detail(words, "stroke-width=" + to_text(*stroke_width));
    }
    if (element.paint_attr.opacity) {
        append_detail(words, "opacity=" + number_text(*element.paint_attr.opacity));
    }

    const std::string transform_text_value = transform_text(transform);
    if (!transform_text_value.empty()) append_detail(words, "transform=" + transform_text_value);

    // A text's character data is a child node, so its words follow every attribute.
    if (std::holds_alternative<TextShape>(element.data) && !text.empty()) {
        append_detail(words, "text=" + quoted(text));
    }
    return words;
}

Words words_of(const NodeShape& shape, const Transform& transform, const std::string& text) {
    if (std::holds_alternative<ElementShape>(shape)) {
        return element_words(std::get<ElementShape>(shape), transform, text);
    }

    // Character data reaches a view on its own only when something asked for it by
    // handle: it has no id to be named by.
    Words words{"<text-node>", {}};
    append_detail(words, "text=" + quoted(std::get<CharacterView>(shape).content));
    return words;
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
        const Words words = words_of(NodeShape{row.shape}, row.transform, row.text);

        std::string prefix;
        if (index > 0) {  // the first row is what the view is rooted at, and it hangs from nothing
            followed.resize(row.depth + 1, false);
            followed[row.depth] = !row.last;
            for (std::size_t level = 1; level < row.depth; ++level) {
                prefix += followed[level] ? "\xE2\x94\x82   " : "    ";
            }
            prefix += row.last ? "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 " : "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 ";
        }
        lines.push_back(Line{std::move(prefix), words.name, joined_details(words)});
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
    const Words words = words_of(view.shape, view.transform, view.text);
    std::string out = words.name;
    if (view.parent) {
        const Words parent = words_of(NodeShape{view.parent->shape}, Transform{}, {});
        out += "  parent=" + parent.name;
    }

    if (words.details.empty()) return out;
    // A root's canvas is one value and reads best on its own line. Everything else is
    // presented as a heading followed by the attributes it carries, one group per line.
    if (!view.parent && words.details.size() == 1) return out + " " + words.details.front();
    for (const std::string& detail : words.details) out += "\n  " + detail;
    return out;
}

std::string render(const View& view) {
    if (std::holds_alternative<TreeView>(view)) return render(std::get<TreeView>(view));
    if (std::holds_alternative<NodeView>(view)) return render(std::get<NodeView>(view));
    if (std::holds_alternative<HistoryView>(view)) return render(std::get<HistoryView>(view));
    return {};
}

namespace {

std::string label(const NodeRef& reference) {
    if (std::holds_alternative<RootRef>(reference)) return "/";
    if (std::holds_alternative<HandleRef>(reference)) {
        return "#" + std::to_string(std::get<HandleRef>(reference).id);
    }
    return quote_if_needed(std::get<SvgIdRef>(reference).name);
}

std::string element_label(std::string_view element, std::string_view svg_id) {
    std::string label{element};
    if (!svg_id.empty()) label += "#" + quote_if_needed(svg_id);
    return label;
}

std::string element_label(const event::NodeName& name) { return element_label(name.element, name.svg_id); }

std::string element_count_text(std::size_t count) {
    return std::to_string(count) + (count == 1 ? " element" : " elements");
}

std::string element_count_suffix(std::size_t count) {
    return " (" + element_count_text(count) + ")";
}

std::string_view empty_container_noun(std::string_view element) {
    if (element == "g") return "group";
    if (element == "defs" || element == "symbol" || element == "switch") {
        return "container";
    }
    return "container";
}

std::string_view system_name(Length::Unit unit) {
    switch (unit) {
        case Length::Unit::Mm:
        case Length::Unit::Cm: return "metric";
        case Length::Unit::In:
        case Length::Unit::Pt:
        case Length::Unit::Px: return "imperial";
        case Length::Unit::UserUnit: return "a user unit, which is whatever the viewport says";
        case Length::Unit::Em: return "an em, which is a font metric";
        case Length::Unit::Ex: return "an ex, which is a font metric";
    }
    return "no system";
}

struct EventText {
    std::string operator()(const event::Created& made) const {
        std::string message = element_label(made.node) + " created";
        if (made.parent_named) {
            message += " in " + element_label(made.parent);
        }
        return message;
    }
    std::string operator()(const event::Deleted& gone) const {
        std::string message = element_label(gone.node) + " deleted";
        if (gone.descendants != 0) message += element_count_suffix(gone.descendants);
        return message;
    }
    std::string operator()(const event::Moved& moved) const {
        std::string message =
            element_label(moved.node) + " moved by (" + to_text(moved.dx) + ", " + to_text(moved.dy) + ")";
        if (moved.mode == EditMode::Cascade) message += element_count_suffix(moved.nodes);
        return message;
    }
    std::string operator()(const event::Scaled& scaled) const {
        std::string message = element_label(scaled.node) + " scaled by " + number_text(scaled.factor);
        if (scaled.mode == EditMode::Cascade) message += element_count_suffix(scaled.nodes);
        return message;
    }
    std::string operator()(const event::AddedTo& moved) const {
        return element_label(moved.node) + " moved to the end of " + element_label(moved.parent);
    }
    std::string operator()(const event::MovedToFront& moved) const {
        return element_label(moved.node) + " moved to the front of " + element_label(moved.parent);
    }
    std::string operator()(const event::MovedToBack& moved) const {
        return element_label(moved.node) + " moved to the back of " + element_label(moved.parent);
    }
    std::string operator()(const event::Grouped& grouped) const {
        return element_label(grouped.node) + " created" + element_count_suffix(grouped.members);
    }
    std::string operator()(const event::Ungrouped& released) const {
        return element_label(released.erased) + " ungrouped into " + element_label(released.parent) +
               element_count_suffix(released.members);
    }
    std::string operator()(const event::AttributeSet& set) const {
        return element_label(set.node) + " updated: " + quote_if_needed(set.attribute) + "=" +
               quote_if_needed(set.value);
    }
    std::string operator()(const event::ContentSet& set) const {
        return element_label(set.node) + " updated: text=" + quoted(set.content);
    }
    std::string operator()(const event::CanvasSet& canvas) const {
        const std::optional<LengthPercentage> width_value =
            present_length_percentage(canvas.width);
        const std::optional<LengthPercentage> height_value =
            present_length_percentage(canvas.height);
        const std::string width = width_value ? to_text(*width_value) : "unset";
        const std::string height = height_value ? to_text(*height_value) : "unset";
        return "the canvas is now " + width + " x " + height + ", and the history starts from here";
    }
    std::string operator()(const event::Undone& undo) const;
    std::string operator()(const event::Redone& redo) const;
    std::string operator()(const event::CheckedOut& checkout) const {
        std::string message = "checked out " + std::to_string(checkout.commit);
        if (checkout.no_effect) message += " (no effect)";
        return message + "\n" + render(checkout.timeline);
    }
    std::string operator()(const event::NothingMoved&) const { return "the offset is zero, nothing moved"; }
    std::string operator()(const event::AlreadyFront& front) const {
        return element_label(front.node) + " is already in front";
    }
    std::string operator()(const event::AlreadyBack& back) const {
        return element_label(back.node) + " is already at the back";
    }
    std::string operator()(const event::Initialized& initialized) const {
        return "canvas " + canvas_size_text(initialized.width, initialized.height) + " created";
    }
    std::string operator()(const event::Opened& opened) const {
        std::string message =
            "read " + quote_if_needed(opened.path) + " (" + element_count_text(opened.elements) + ")";
        for (const AssignedId& assigned : opened.assigned_ids) {
            if (assigned.original_id.empty()) {
                message += "\nwarning: assigned id " + quote_if_needed(assigned.id) + " to unnamed <" +
                           assigned.element + ">";
            } else {
                message += "\nwarning: id " + quoted(assigned.original_id) +
                           " is reserved for the root, renamed <" + assigned.element + "> to " +
                           quote_if_needed(assigned.id);
            }
        }
        return message;
    }
    std::string operator()(const event::Saved& saved) const {
        return "saved to " + quote_if_needed(saved.path) + " (" + element_count_text(saved.elements) + ")";
    }
    template <class T>
    std::string operator()(const T&) const {
        static_assert(always_false<T>, "no text for this event alternative");
        return {};
    }
};

std::string EventText::operator()(const event::Undone& undo) const {
    std::string message =
        undo.original ? "undo: " + std::visit(*this, *undo.original)
                      : "undone, at " + std::to_string(undo.position) + " of " + std::to_string(undo.depth) + " edits";
    message += " (HEAD " + std::to_string(undo.checkout) + ")";
    return message;
}

std::string EventText::operator()(const event::Redone& redo) const {
    std::string message =
        redo.original ? "redo: " + std::visit(*this, *redo.original)
                      : "redone, at " + std::to_string(redo.position) + " of " + std::to_string(redo.depth) + " edits";
    message += " (HEAD " + std::to_string(redo.checkout) + ")";
    return message;
}

struct RefusalText {
    std::string operator()(const fault::NoDocument&) const {
        return "error: no document. use 'init' or 'read' first";
    }
    std::string operator()(const fault::MissingShape& missing) const {
        return "error: no such shape: " + label(missing.node);
    }
    std::string operator()(const fault::MissingContainer& missing) const {
        return "error: no such container: " + label(missing.node);
    }
    std::string operator()(const fault::NotContainer& wrong) const {
        return "error: not a container: " + label(wrong.node);
    }
    std::string operator()(const fault::DuplicateId& taken) const {
        return "error: id already exists: " + quote_if_needed(taken.id);
    }
    std::string operator()(const fault::InvalidId& invalid) const {
        return "error: invalid XML id: " + quote_if_needed(invalid.id);
    }
    std::string operator()(const fault::ReservedId& reserved) const {
        return "error: id " + quoted(reserved.id) + " is reserved for the root";
    }
    std::string operator()(const fault::DuplicateGroupMember& duplicate) const {
        return "error: duplicate node in group: " + label(duplicate.node);
    }
    std::string operator()(const fault::UnknownAttribute& attribute) const {
        return "error: unknown attribute: " + quote_if_needed(attribute.name);
    }
    std::string operator()(const fault::NegativeDimension& dimension) const {
        return "error: " + quote_if_needed(dimension.attribute) + " must not be negative";
    }
    std::string operator()(const fault::InvalidPoints&) const {
        return "error: a polygon needs at least three coordinate pairs";
    }
    std::string operator()(const fault::OpacityOutOfRange& opacity) const {
        return "error: opacity must be between 0 and 1, not " + number_text(opacity.value);
    }
    std::string operator()(const fault::NonPositiveCanvas& dimension) const {
        return "error: canvas " + quote_if_needed(dimension.attribute) + " must be positive";
    }
    std::string operator()(const fault::NonFiniteValue& value) const {
        return "error: " + quote_if_needed(value.name) + " must be a finite number";
    }
    std::string operator()(const fault::CannotUngroupNonContainer& leaf) const {
        return "error: cannot ungroup a shape or text node: " + label(leaf.node);
    }
    std::string operator()(const fault::CannotUngroupComposite& composite) const {
        return "error: cannot ungroup composite shape: " + label(composite.node);
    }
    std::string operator()(const fault::CannotGroupIntoOwnDescendant& descendant) const {
        return "error: cannot group a container into its own descendant: " + label(descendant.node);
    }
    std::string operator()(const fault::CannotAddToOwnDescendant& descendant) const {
        return "error: cannot add a container to its own descendant: " + label(descendant.node);
    }
    std::string operator()(const fault::CannotGroupTopLevelContainer& top) const {
        return "error: cannot group top-level container: " + quote_if_needed(top.name);
    }
    std::string operator()(const fault::CannotUngroupTopLevelContainer& top) const {
        return "error: cannot ungroup top-level container: " + quote_if_needed(top.name);
    }
    std::string operator()(const fault::ScaleFactorNotPositive&) const {
        return "error: scale factor must be positive";
    }
    std::string operator()(const fault::TransformHasNoScope&) const {
        return "error: transform does not take cascade or restrict";
    }
    std::string operator()(const fault::EmptyContainer& empty) const {
        std::string message = "error: cannot move or scale an empty " +
                              std::string{empty_container_noun(empty.element)};
        if (empty.element != "g") message = "error: cannot move or scale an empty <" + empty.element + "> container";
        return message + ": " + element_label(empty.element, empty.svg_id);
    }
    std::string operator()(const fault::NoDirectParams&) const {
        return "error: this node has no direct params";
    }
    std::string operator()(const fault::RootCannotBeErased&) const {
        return "error: the root cannot be erased";
    }
    std::string operator()(const fault::RootHasNoSiblings&) const {
        return "error: the root is the whole document: it has no siblings to stand in front of or behind";
    }
    std::string operator()(const fault::EmptyGroup&) const {
        return "error: a group needs at least one node to hold";
    }
    std::string operator()(const fault::NothingToUndo&) const { return "error: there is nothing left to undo"; }
    std::string operator()(const fault::UndoAfterIo&) const {
        return "error: undo not allowed after io command";
    }
    std::string operator()(const fault::NothingToRedo&) const { return "error: there is nothing left to redo"; }
    std::string operator()(const fault::UnknownCommit& missing) const {
        return "error: no such commit: " + std::to_string(missing.commit);
    }
    std::string operator()(const fault::NoRootElement&) const {
        return "error: the document has no root element";
    }
    std::string operator()(const fault::RootIsNotContainer&) const {
        return "error: the root is not a container";
    }
    std::string operator()(const fault::RootIsNotSvg&) const {
        return "error: the root is not an <svg> element, so it has no canvas";
    }
    std::string operator()(const fault::MissingSourcePath&) const {
        return "error: this document came from nowhere yet: save <path> says where to write it";
    }
    std::string operator()(const fault::CannotReadFile& missing) const {
        return "error: cannot read file: " + quote_if_needed(missing.path);
    }
    std::string operator()(const fault::CannotWriteFile& missing) const {
        return "error: cannot write file: " + quote_if_needed(missing.path);
    }
    std::string operator()(const fault::InvalidSvg& invalid) const {
        const SvgDiagnostic& diagnostic = invalid.diagnostic;
        std::string message = "error: cannot parse SVG";
        if (diagnostic.line != 0) message += " at line " + std::to_string(diagnostic.line);
        message += ": ";
        switch (diagnostic.problem) {
            case SvgProblem::MalformedXml: return message + "malformed XML";
            case SvgProblem::NoRootElement: return message + "the XML has no root element";
            case SvgProblem::RootIsNotSvg: return message + "the root element must be <svg>";
            case SvgProblem::UnsupportedElement:
                return message + "unsupported element <" + diagnostic.element + ">";
            case SvgProblem::UnsupportedAttribute:
                return message + "unsupported attribute " + quote_if_needed(diagnostic.attribute) + " on <" +
                       diagnostic.element + ">";
            case SvgProblem::InvalidAttributeValue:
                return message + "invalid value " + quote_if_needed(diagnostic.value) + " for " +
                       quote_if_needed(diagnostic.attribute) + " on <" + diagnostic.element + ">";
            case SvgProblem::DuplicateId:
                return message + "duplicate id " + quote_if_needed(diagnostic.value);
            case SvgProblem::InvalidId:
                return message + "invalid id " + quote_if_needed(diagnostic.value);
            case SvgProblem::UnexpectedText: return message + "text outside a text element";
        }
        return message + "invalid SVG";
    }
    std::string operator()(const fault::IncompatibleLengths& lengths) const {
        const std::string offset = to_text(lengths.offset);
        const std::string value = to_text(lengths.value);
        std::string message = "error: the offset " + offset + " does not add up with " + value;
        if (std::holds_alternative<Percentage>(lengths.offset) || std::holds_alternative<Percentage>(lengths.value)) {
            message += ": a percentage has no sum with a length";
        } else {
            message += ": " + offset + " is " +
                       std::string{system_name(std::get<Length>(lengths.offset).unit)} + " and " + value + " is " +
                       std::string{system_name(std::get<Length>(lengths.value).unit)};
        }
        return message +
               " - mm and cm are metric, in, pt and px are defined against the inch, and the two systems are not mixed";
    }
    std::string operator()(const fault::TextCannotMove&) const {
        return "error: a text node has no coordinates and no transform to move";
    }
    std::string operator()(const fault::TextCannotScale&) const {
        return "error: a text node has no coordinates and no transform to scale";
    }
    std::string operator()(const fault::InternalError&) const { return "error: internal error"; }
    template <class T>
    std::string operator()(const T&) const {
        static_assert(always_false<T>, "no text for this refusal alternative");
        return {};
    }
};

struct OutcomeText {
    std::string operator()(const View& view) const { return render(view); }
    std::string operator()(const Event& event) const { return render(event); }
    std::string operator()(const Refusal& refusal) const { return render(refusal); }
    template <class T>
    std::string operator()(const T&) const {
        static_assert(always_false<T>, "no text for this outcome alternative");
        return {};
    }
};

}  // namespace

std::string render(const Event& event) { return std::visit(EventText{}, event); }

std::string render(const Refusal& refusal) { return std::visit(RefusalText{}, refusal); }

std::string render(const HistoryView& view) {
    struct HistoryRow {
        std::size_t position;
        CommitId commit;
        std::optional<std::size_t> entry;
        bool baseline;
    };

    const event::HistoryEntries& entries = view.entries.get();
    const event::HistoryCursor& cursor = view.cursor.get();
    const std::size_t checkpoints = cursor.depth + 1;

    // Select up to `limit` neighboring checkpoints around HEAD, split as evenly as
    // possible. If one side reaches an end of the branch, the other side grows. The
    // baseline, newest commit, and HEAD remain anchors outside the ordinary window.
    std::vector<bool> selected(checkpoints, view.limit >= checkpoints - 1);
    if (view.limit < checkpoints - 1 && view.limit != 0) {
        selected[cursor.position] = true;
        const std::size_t above_target = view.limit / 2;
        std::size_t above = std::min(above_target, cursor.depth - cursor.position);
        std::size_t below = std::min(view.limit - above, cursor.position);
        std::size_t remaining = view.limit - above - below;

        const std::size_t extra_below = std::min(remaining, cursor.position - below);
        below += extra_below;
        remaining -= extra_below;
        const std::size_t extra_above =
            std::min(remaining, cursor.depth - cursor.position - above);
        above += extra_above;

        for (std::size_t offset = 1; offset <= above; ++offset) {
            selected[cursor.position + offset] = true;
        }
        for (std::size_t offset = 1; offset <= below; ++offset) {
            selected[cursor.position - offset] = true;
        }
    }
    if (view.limit < checkpoints - 1) {
        selected[0] = true;
        selected[cursor.position] = true;
        selected[cursor.depth] = true;
    }

    std::vector<HistoryRow> rows;
    for (std::size_t position = cursor.depth; position != 0; --position) {
        if (!selected[position] || position > entries.size()) continue;
        const event::HistoryEntry& entry = entries[position - 1];
        if (entry.position != position) continue;
        rows.push_back({position, entry.commit, position - 1, false});
    }
    if (cursor.baseline != 0) rows.push_back({0, cursor.baseline, std::nullopt, true});

    std::size_t commit_width = 0;
    for (const HistoryRow& row : rows) {
        commit_width = std::max(commit_width, std::to_string(row.commit).size());
    }

    std::string out;
    auto append_commit = [&out, commit_width](CommitId commit) {
        const std::string text = std::to_string(commit);
        out.append(commit_width - text.size(), ' ');
        out += text;
    };

    for (std::size_t index = 0; index < rows.size(); ++index) {
        const HistoryRow& row = rows[index];
        if (index > 0) {
            const std::size_t folded = rows[index - 1].position > row.position
                                           ? rows[index - 1].position - row.position - 1
                                           : 0;
            if (folded != 0) {
                out += "\n... " + std::to_string(folded);
                out += folded == 1 ? " commit folded ..." : " commits folded ...";
            }
        }
        if (!out.empty()) out += '\n';
        append_commit(row.commit);
        if (row.baseline) {
            out += "  baseline";
        } else if (row.entry) {
            out += "  " + std::visit(EventText{}, entries[*row.entry].outcome);
        }
        if (row.commit == cursor.head) out += " (HEAD)";
    }
    return out.empty() ? "no history" : out;
}

std::string render(const Outcome& outcome) { return std::visit(OutcomeText{}, outcome.result); }

