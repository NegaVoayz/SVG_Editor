// The Model on its own: the tree, the transactions, and the checkpoints of the
// one hive the tree lives in.
#include <cstdio>
#include <exception>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "Document.hh"

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::printf("FAIL line %d: %s\n", line, expression);
    ++failures;
}

Node group(std::string svg_id) {
    SVGNode element;
    element.identity_attr.id = std::move(svg_id);
    element.data = Container{ChildAttr{}, Container::Group{}};
    return Node{std::move(element)};
}

Node rect(std::string svg_id) {
    SVGNode element;
    element.identity_attr.id = std::move(svg_id);
    element.data = Shape{Shape::Rect{Length{0.0F, Length::Unit::UserUnit}, Length{0.0F, Length::Unit::UserUnit},
                                     Length{10.0F, Length::Unit::UserUnit},
                                     Length{10.0F, Length::Unit::UserUnit}}};
    return Node{std::move(element)};
}

template<typename Action>
bool throws(Action&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

// The canvas of a document: the width and height the root element carries.
std::optional<Container::SVG> canvas_of(const Document& document) {
    const std::optional<Node> payload = document.node(document.root());
    if (!payload || !std::holds_alternative<SVGNode>(*payload)) return std::nullopt;
    const SVGNode& element = std::get<SVGNode>(*payload);
    if (!std::holds_alternative<Container>(element.data)) return std::nullopt;
    const Container& container = std::get<Container>(element.data);
    if (!std::holds_alternative<Container::SVG>(container.specific)) return std::nullopt;
    return std::get<Container::SVG>(container.specific);
}

// A payload that claims it holds children but has nowhere to keep the list: it does not
// count, because the capability is the storage the tree writes, not a statement about it.
struct ClaimsChildren {
    static constexpr bool holds_children = true;
};

// One that does have the child-list ends counts, whatever it is called.
struct HasLink {
    ChildAttr child_attr;
};

// Child-list ends that cannot hold node handles are not child storage either.
struct NarrowLink {
    struct { int first_child, last_child; } child_attr;
};

}  // namespace

#define CHECK(condition) check((condition), #condition, __LINE__)

// A document owns its hive and its history. The hive's storage cannot be moved, so
// neither can the document that holds it by value.
static_assert(!std::is_copy_constructible_v<Document>);
static_assert(!std::is_move_constructible_v<Document>);

// Whether a payload can hold children is a compile-time property of its type, and the types
// that have it are exactly the ones with the first/last child storage. No list of
// element kinds is written anywhere, and a claim without the storage does not
// satisfy it.
static_assert(ChildHolder<Container>);
static_assert(ChildHolder<Text>);
static_assert(!ChildHolder<Shape>);
static_assert(!ChildHolder<PlainText>);
static_assert(!ChildHolder<SVGNode>);
static_assert(Element<SVGNode>);
static_assert(!Element<PlainText>);
static_assert(ChildHolder<HasLink>);
static_assert(!ChildHolder<ClaimsChildren>);
static_assert(!ChildHolder<NarrowLink>);

// A node is the whole record the hive stores, so every alternative of Node carries
// its parent and sibling links; a type without them could not be a node.
struct Unheld {};
static_assert(Linked<SVGNode>);
static_assert(Linked<PlainText>);
static_assert(Linked<Removed>);
static_assert(!Linked<Unheld>);
static_assert(!ChildHolder<Removed>);

int main() {
    Document document;
    const Id root = document.root();

    // the root is the first virtual id of the hive: a link that leads nowhere is
    // no_id, not a reserved id in front of it
    CHECK(root != no_id);
    const std::optional<Node> root_payload = document.node(root);
    CHECK(root_payload.has_value());
    CHECK(std::holds_alternative<SVGNode>(*root_payload));
    CHECK(document.has_node(root));
    CHECK(!document.has_node(no_id));
    CHECK(document.children(root).empty());
    CHECK(!document.can_undo());
    CHECK(document.source_path().empty());

    // the canvas belongs to the root element, and a fresh document has one
    {
        const std::optional<Container::SVG> canvas = canvas_of(document);
        CHECK(canvas.has_value());
        if (canvas) {
            CHECK(canvas->width.has_value());
            CHECK(canvas->height.has_value());
            if (canvas->width) CHECK(to_text(*canvas->width) == "800");
            if (canvas->height) CHECK(to_text(*canvas->height) == "600");
        }
    }

    // one transaction is one block, so both children go back together
    Id first = no_id;
    Id second = no_id;
    {
        Transaction edit = document.transaction();
        first = edit.append_child(root, group("first"));
        second = edit.append_child(root, group("second"));
        edit.commit();
    }
    CHECK(document.children(root) == std::vector<Id>({first, second}));
    CHECK(document.find("second") == second);
    CHECK(document.depth() == 1);
    CHECK(document.position() == 1);

    // a nested append and a payload edit in the same transaction
    {
        Transaction edit = document.transaction();
        const Id inner = edit.append_child(first, group("inner"));
        edit.update(inner, group("renamed"));
        edit.commit();
    }
    CHECK(document.find("inner") == std::nullopt);
    CHECK(document.find("renamed").has_value());
    CHECK(document.depth() == 2);

    // undo takes the whole block back, redo brings it forward
    document.undo();
    CHECK(document.children(first).empty());
    CHECK(document.can_redo());
    document.redo();
    CHECK(document.children(first).size() == 1);

    // A payload update and a move are the same kind of history step now: one
    // commit on the one hive, reached through the same checkpoint list.
    {
        Transaction edit = document.transaction();
        edit.update(second, group("second-renamed"));
        edit.commit();
    }
    document.undo();
    CHECK(document.find("second") == second);
    CHECK(document.children(root).size() == 2);
    document.redo();
    CHECK(document.find("second-renamed") == second);
    CHECK(document.children(root).size() == 2);

    // an update is a new node version: the hive never rewrites a payload in place
    CHECK(document.node(second).has_value());

    // erasing takes the payloads away, undo brings them back
    {
        Transaction edit = document.transaction();
        edit.erase(first);
        edit.commit();
    }
    CHECK(!document.node(first).has_value());
    CHECK(!document.has_node(first));
    CHECK(document.children(root) == std::vector<Id>({second}));
    CHECK(document.find("renamed") == std::nullopt);
    document.undo();
    CHECK(document.node(first).has_value());
    CHECK(document.has_node(first));
    CHECK(document.children(root).size() == 2);
    CHECK(document.find("renamed").has_value());

    // back at the head: the erase of `first` is applied, so the subtree is gone
    document.redo();
    CHECK(!document.can_redo());
    CHECK(document.children(root) == std::vector<Id>({second}));

    // an edit that is never committed rolls back when the transaction dies
    const std::size_t depth = document.depth();
    {
        Transaction edit = document.transaction();
        edit.append_child(root, group("never"));
    }
    CHECK(document.find("never") == std::nullopt);
    CHECK(document.depth() == depth);
    CHECK(!document.can_redo());
    CHECK(document.children(root).size() == 1);

    // the root is not erasable, and the failure leaves the tree as it was
    {
        Transaction edit = document.transaction();
        bool threw = false;
        try {
            edit.erase(root);
        } catch (const std::exception&) {
            threw = true;
        }
        CHECK(threw);
    }
    CHECK(document.children(root) == std::vector<Id>({second}));

    // the failed transaction closed itself, so the next edit starts clean
    {
        Transaction edit = document.transaction();
        const Id added = edit.append_child(root, group("after-failure"));
        edit.commit();
        CHECK(added != no_id);
    }
    CHECK(document.find("after-failure").has_value());
    CHECK(document.children(root).size() == 2);

    // walking back two blocks brings the erased subtree and its rename with it
    document.undo();
    document.undo();
    CHECK(document.children(root) == std::vector<Id>({first, second}));
    CHECK(document.find("first") == first);
    CHECK(document.find("renamed").has_value());

    // Forward again. Virtual ids are never reused, so the node created after the
    // undo has an id of its own; the erased `first` id comes back only when the
    // snapshot that still shows that node is selected again.
    document.redo();
    document.redo();
    const std::vector<Id> children = document.children(root);
    CHECK(children.size() == 2);
    CHECK(children[0] == second);
    CHECK(children[1] != first);
    CHECK(document.find("after-failure") == children[1]);
    CHECK(document.find("first") == std::nullopt);

    // Erasing the last child splices the parent's own link instead of a sibling's,
    // which is the other end of the same list.
    Id third = no_id;
    {
        Transaction edit = document.transaction();
        third = edit.append_child(root, group("third"));
        edit.commit();
    }
    CHECK(document.find("third") == third);
    {
        Transaction edit = document.transaction();
        edit.erase(third);
        edit.commit();
    }
    CHECK(!document.node(third).has_value());
    CHECK(document.find("third") == std::nullopt);
    document.undo();
    CHECK(document.find("third") == third);
    CHECK(document.position() + 1 == document.depth());

    // --- moving a node, and what the tree refuses ---
    const std::size_t before = document.children(root).size();
    Id holder = no_id;
    Id leaf = no_id;
    Id shape = no_id;
    {
        Transaction edit = document.transaction();
        holder = edit.append_child(root, group("holder"));
        leaf = edit.append_child(root, group("leaf"));
        shape = edit.append_child(root, rect("box"));
        edit.commit();
    }
    CHECK(document.children(root).size() == before + 3);
    CHECK(document.children(root)[before] == holder);
    CHECK(document.children(root)[before + 1] == leaf);
    CHECK(document.children(root)[before + 2] == shape);

    // An update replaces the payload and leaves the node's place in the tree alone,
    // which is what makes a translation one handle wide.
    {
        Transaction edit = document.transaction();
        edit.update(leaf, group("leaf-renamed"));
        edit.commit();
    }
    CHECK(document.find("leaf") == std::nullopt);
    CHECK(document.find("leaf-renamed") == leaf);
    CHECK(document.children(holder).empty());

    {
        Transaction edit = document.transaction();
        edit.attach_last(holder, leaf);
        edit.commit();
    }
    CHECK(document.children(holder) == std::vector<Id>({leaf}));
    CHECK(document.find("leaf-renamed") == leaf);

    // An update replaces what a node holds, not what it is: a payload that cannot hold
    // children cannot take over a node whose type has the child-list storage.
    {
        Transaction edit = document.transaction();
        CHECK(throws([&] { edit.update(shape, group("shape-becoming-a-group")); }));
        CHECK(throws([&] { edit.update(holder, rect("holder-becoming-a-rect")); }));
    }
    CHECK(document.find("shape-becoming-a-group") == std::nullopt);
    CHECK(document.children(shape).empty());

    document.undo();
    CHECK(document.children(holder).empty());
    document.redo();
    CHECK(document.children(holder) == std::vector<Id>({leaf}));

    // A node cannot be moved inside itself, the root cannot be moved at all, a
    // shape cannot take children, and the root cannot be erased: in every case the
    // tree is left exactly as it was.
    {
        Transaction edit = document.transaction();
        CHECK(throws([&] { edit.attach_last(leaf, holder); }));
        CHECK(throws([&] { edit.attach_last(leaf, leaf); }));
        CHECK(throws([&] { edit.attach_last(leaf, root); }));
        CHECK(throws([&] { edit.attach_last(shape, leaf); }));
        CHECK(throws([&] { edit.append_child(shape, group("inside-a-rect")); }));
        CHECK(throws([&] { edit.erase(root); }));
    }
    CHECK(document.children(holder) == std::vector<Id>({leaf}));
    CHECK(document.find("inside-a-rect") == std::nullopt);
    // the shape is still under the root, and the leaf still under the holder
    CHECK(document.children(root).size() == before + 2);

    // --- placing a child in front of a sibling ---

    // A new node in front of the first child is a splice at the other end of the
    // chain than an append: the same list, walked from its oldest link.
    Id frontmost = no_id;
    {
        Transaction edit = document.transaction();
        frontmost = edit.insert_at(root, document.children(root)[0], group("frontmost"));
        edit.commit();
    }
    CHECK(document.children(root)[0] == frontmost);
    CHECK(document.find("frontmost") == frontmost);
    document.undo();
    CHECK(document.find("frontmost") == std::nullopt);
    CHECK(document.children(root)[0] != frontmost);
    document.redo();
    CHECK(document.children(root)[0] == frontmost);

    // A node moved in front of one of its own siblings keeps its handle and its
    // subtree, and it lands where it was asked to.
    const std::vector<Id> order = document.children(root);
    const Id last_child = order.back();
    {
        Transaction edit = document.transaction();
        edit.attach_at(root, order[0], last_child);
        edit.commit();
    }
    const std::vector<Id> reordered = document.children(root);
    CHECK(reordered.front() == last_child);
    CHECK(reordered.size() == order.size());
    {
    CHECK(document.node(last_child).has_value());
        std::vector<Id> expected{last_child};
        expected.insert(expected.end(), order.begin(), order.end() - 1);
        CHECK(reordered == expected);
    }
    document.undo();
    CHECK(document.children(root) == order);
    document.redo();
    CHECK(document.children(root) == reordered);

    // an anchor that is not a child of the parent, and a node asked to sit in front
    // of itself, are both refused
    {
        Transaction edit = document.transaction();
        CHECK(throws([&] { edit.attach_at(holder, root, leaf); }));
        CHECK(throws([&] { edit.insert_at(holder, root, group("misplaced")); }));
        CHECK(throws([&] { edit.attach_at(root, leaf, leaf); }));
    }
    CHECK(document.children(root) == reordered);
    CHECK(document.find("misplaced") == std::nullopt);
    CHECK(document.children(holder) == std::vector<Id>({leaf}));

    // --- the context: the canvas, the reset, and the checkpoint ---

    // A reset is a fresh document: an empty tree, the canvas it was given for
    // whichever sides it was given, the virtual ids from the start again, and
    // nothing to undo back into.
    document.reset(Length{1024.0F, Length::Unit::Px}, std::nullopt);
    CHECK(document.children(document.root()).empty());
    CHECK(document.find("after-failure") == std::nullopt);
    CHECK(document.depth() == 0);
    CHECK(document.position() == 0);
    CHECK(!document.can_undo());
    CHECK(!document.can_redo());
    CHECK(document.source_path().empty());
    {
        const std::optional<Container::SVG> canvas = canvas_of(document);
        CHECK(canvas.has_value());
        if (canvas) {
            if (canvas->width) CHECK(to_text(*canvas->width) == "1024px");
            // the side that was left out keeps the default
            if (canvas->height) CHECK(to_text(*canvas->height) == "600");
        }
    }

    // the history of the new document starts at its own baseline
    Id kept = no_id;
    {
        Transaction edit = document.transaction();
        kept = edit.append_child(document.root(), group("after-reset"));
        edit.commit();
    }
    CHECK(document.depth() == 1);
    document.undo();
    CHECK(document.find("after-reset") == std::nullopt);
    CHECK(!document.can_undo());  // and there is nothing behind the baseline
    document.redo();
    CHECK(document.find("after-reset") == kept);

    // A checkpoint drops the way back and keeps the tree. A save is one, and until
    // the serializer exists the other half of that seam is what can be checked: a
    // document with no file behind it has no path to write to, and a save that
    // failed is not a checkpoint.
    CHECK(!document.save(std::nullopt).ok);
    CHECK(!document.save("no-such-directory/out.svg").ok);
    CHECK(document.source_path().empty());
    CHECK(document.can_undo());

    {
        Transaction edit = document.transaction();
        edit.append_child(document.root(), group("newer"));
        edit.commit();
    }
    CHECK(document.depth() == 2);

    document.clear_history();
    CHECK(document.depth() == 0);
    CHECK(document.position() == 0);
    CHECK(!document.can_undo());
    CHECK(!document.can_redo());
    // the tree stays: what the history forgot is the way back to it
    CHECK(document.find("after-reset") == kept);
    CHECK(document.find("newer").has_value());

    {
        Transaction edit = document.transaction();
        edit.erase(kept);
        edit.commit();
    }
    CHECK(document.depth() == 1);
    document.undo();
    CHECK(document.find("after-reset") == kept);  // the checkpoint is the floor
    CHECK(document.find("newer").has_value());
    CHECK(!document.can_undo());

    if (failures != 0) {
        std::printf("document: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("document: all checks passed\n");
    return 0;
}
