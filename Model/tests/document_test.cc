// The Model on its own: the tree, the transactions, and the history that has to
// keep the two hives of the arena in step.
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
    element.data = Container{ChildAttr{no_id}, Container::Group{}};
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

}  // namespace

#define CHECK(condition) check((condition), #condition, __LINE__)

// A document owns its arena and its history, so it moves and does not copy; the
// move is a member-wise one, with no heap indirection of its own.
static_assert(!std::is_copy_constructible_v<Document>);
static_assert(std::is_nothrow_move_constructible_v<Document>);

int main() {
    Document document;
    const Id root = document.root();

    // the root is the first payload of the arena: a link that leads nowhere is
    // no_id, not a reserved slot in front of it
    CHECK(root != no_id);
    const std::optional<Node> root_payload = document.node(root);
    CHECK(root_payload.has_value());
    CHECK(std::holds_alternative<SVGNode>(*root_payload));
    CHECK(document.children(root).empty());
    CHECK(!document.can_undo());

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

    // A transaction that only wrote to the node hive still has to land on the same
    // diff id in the edge hive: this is why the history is walked by seek.
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

    // an update is a payload patch: the hive never rewrites a payload in place
    CHECK(document.node(second).has_value());

    // erasing takes the payloads away, undo brings them back
    {
        Transaction edit = document.transaction();
        edit.erase(first);
        edit.commit();
    }
    CHECK(!document.node(first).has_value());
    CHECK(document.children(root) == std::vector<Id>({second}));
    CHECK(document.find("renamed") == std::nullopt);
    document.undo();
    CHECK(document.node(first).has_value());
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

    // Forward again. The slot the erase freed was handed to the next insert, so
    // the `first` handle now names that new node: a handle names a slot, and only
    // for as long as the node in it lives - the durable name is the SVG id.
    document.redo();
    document.redo();
    const std::vector<Id> children = document.children(root);
    CHECK(children.size() == 2);
    CHECK(children[0] == second);
    CHECK(children[1] == first);
    CHECK(document.find("after-failure") == first);
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

    if (failures != 0) {
        std::printf("document: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("document: all checks passed\n");
    return 0;
}
