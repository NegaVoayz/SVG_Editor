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

#include <charconv>
#include <cmath>
#include <limits>

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

Node svg_root() {
    SVGNode element;
    element.data = Container{ChildAttr{}, Container::SVG{}};
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

std::optional<TouchedKind> kind_of(const TouchedNodes& touched, Id id) {
    for (const TouchedNode& change : touched) {
        if (change.node == id) return change.kind;
    }
    return std::nullopt;
}

// A payload that claims it holds children but has nowhere to keep the list: it does not
// count, because the capability is the storage the tree writes, not a statement about it.
struct ClaimsChildren {
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
static_assert(!Linked<Unheld>);

int main() {
    // A float is printed in fixed notation with at most six decimal places: large values
    // stay readable and values such as 0.3 do not become a long binary approximation.
    CHECK(number_text(1.0F / 3.0F) == "0.333333");
    CHECK(number_text(1.234567F) == "1.234567");
    CHECK(number_text(1234567.89F) == "1234567.875");
    CHECK(number_text(0.3F) == "0.3");
    CHECK(number_text(5.0F) == "5");
    CHECK(number_text(-0.0F) == "0");
    CHECK(number_text(1.0e-7F) == exact_number_text(1.0e-7F));
    CHECK(number_text(-5.0e-7F) == exact_number_text(-5.0e-7F));

    // File output is the shortest exact spelling, so small values survive a save and
    // large values do not expose float32's exact decimal expansion.
    const float exact_values[] = {1.0e-7F, 5.0e-7F, 1.0e-6F, -0.0F, 0.3F, 1234567.89F, 1.0e20F,
                                  std::numeric_limits<float>::max()};
    for (const float expected : exact_values) {
        const std::string text = exact_number_text(expected);
        float parsed = 0.0F;
        const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        CHECK(result.ec == std::errc{});
        CHECK(result.ptr == text.data() + text.size());
        CHECK(parsed == expected);
    }
    CHECK(exact_number_text(-0.0F) == "0");
    CHECK(exact_number_text(1.0e-7F) != "0");
    CHECK(exact_number_text(1.0e20F).size() < 16);
    CHECK(svg_text(Length{1.0e-7F, Length::Unit::UserUnit}) == exact_number_text(1.0e-7F));

    // Absence has its own alternative, while present lengths retain their exact type.
    CHECK(std::holds_alternative<Unset>(MaybeLengthPercentage{}));
    CHECK(!present_length_percentage(Unset{}).has_value());
    const MaybeLengthPercentage length = Length{12.0F, Length::Unit::Px};
    const std::optional<LengthPercentage> present_length = present_length_percentage(length);
    CHECK(present_length && std::holds_alternative<Length>(*present_length));
    if (present_length) CHECK(std::get<Length>(*present_length).value == 12.0F);
    const MaybeLengthPercentage percentage = Percentage{25.0F};
    const std::optional<LengthPercentage> present_percentage = present_length_percentage(percentage);
    CHECK(present_percentage && std::holds_alternative<Percentage>(*present_percentage));
    if (present_percentage) CHECK(std::get<Percentage>(*present_percentage).value == 25.0F);

    Document document;
    CHECK(!document.has_document());
    CHECK(document.root() == no_id);
    CHECK(document.children(no_id).empty());
    CHECK(document.depth() == 0);
    CHECK(!document.can_undo());
    CHECK(throws([&] { auto edit = document.transaction(); }));
    // `init` is what makes the first root; before it there is no document to edit.
    document.reset(Unset{}, Unset{});
    const Id root = document.root();
    CHECK(document.undo_barred_by_io());

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
    CHECK(document.element_count() == 0);

    // the canvas belongs to the root element, and a fresh document has one
    {
        const std::optional<Container::SVG> canvas = canvas_of(document);
        CHECK(canvas.has_value());
        if (canvas) {
            const std::optional<LengthPercentage> width = present_length_percentage(canvas->width);
            const std::optional<LengthPercentage> height = present_length_percentage(canvas->height);
            CHECK(width.has_value());
            CHECK(height.has_value());
            if (width) CHECK(to_text(*width) == "800");
            if (height) CHECK(to_text(*height) == "600");
        }
    }

    // one transaction is one block, so both children go back together
    {
        Transaction empty_edit = document.transaction();
        const std::size_t before = document.position();
        const CommitId before_commit = document.commit_id();
        CHECK(empty_edit.empty());
        CHECK(document.position() == before);
        CHECK(document.commit_id() == before_commit);
    }

    Id first = no_id;
    Id second = no_id;
    {
        Transaction edit = document.transaction();
        CHECK(edit.empty());
        first = edit.append_child(root, group("first"));
        CHECK(!edit.empty());
        second = edit.append_child(root, group("second"));
        edit.commit();
    }
    CHECK(document.children(root) == std::vector<Id>({first, second}));
    CHECK(document.find("second") == second);
    CHECK(document.element_count() == 2);
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
    CHECK(document.element_count() == 3);
    CHECK(document.depth() == 2);

    // undo takes the whole block back, redo brings it forward
    const std::optional<Id> inner = document.find("renamed");
    CHECK(inner.has_value());
    const TouchedNodes undone = document.undo();
    CHECK(document.children(first).empty());
    CHECK(document.can_redo());
    CHECK(kind_of(undone, first) == TouchedKind::Update);
    if (inner) CHECK(kind_of(undone, *inner) == TouchedKind::Remove);
    const TouchedNodes redone = document.redo();
    CHECK(document.children(first).size() == 1);
    CHECK(kind_of(redone, first) == TouchedKind::Update);
    if (inner) CHECK(kind_of(redone, *inner) == TouchedKind::Insert);

    // A payload update and a move are the same kind of history step now: one
    // commit on the one hive, reached through the same checkout.
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
    const TouchedNodes erase_undone = document.undo();
    CHECK(document.node(first).has_value());
    CHECK(document.has_node(first));
    CHECK(document.children(root).size() == 2);
    CHECK(document.find("renamed").has_value());
    CHECK(kind_of(erase_undone, first) == TouchedKind::Insert);
    CHECK(kind_of(erase_undone, root) == TouchedKind::Update);  // the parent's first-child link changed too

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

    // --- placing a child before a sibling ---

    // A new node before the first child is a splice at the other end of the
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

    // A staged tree replaces the full document in one model operation. Invalid values
    // are rejected before the current tree is touched.
    {
        Document staged;
        staged.replace(StagedTree{{StagedNode{svg_root(), {1}}, StagedNode{rect("staged"), {}}}, 0});
        CHECK(staged.has_document());
        CHECK(staged.find("staged").has_value());
        CHECK(staged.element_count() == 1);
        CHECK(staged.depth() == 0);
        CHECK(!staged.can_undo());
        CHECK(staged.undo_barred_by_io());

        const Id preserved_root = staged.root();
        const Id preserved_child = *staged.find("staged");
        CHECK(throws([&] {
            staged.replace(StagedTree{{StagedNode{svg_root(), {1}}, StagedNode{rect("leaf-parent"), {2}},
                                      StagedNode{rect("child"), {}}},
                                     0});
        }));
        CHECK(staged.root() == preserved_root);
        CHECK(staged.find("staged") == preserved_child);
        CHECK(!staged.find("leaf-parent").has_value());
    }

    // A reset is a fresh document: an empty tree, the canvas it was given for
    // whichever sides it was given, the virtual ids from the start again, and
    // nothing to undo back into.
    document.reset(Length{1024.0F, Length::Unit::Px}, Unset{});
    CHECK(document.children(document.root()).empty());
    CHECK(document.find("after-failure") == std::nullopt);
    CHECK(document.commit_id() != 0);
    CHECK(document.depth() == 0);
    CHECK(document.position() == 0);
    CHECK(!document.can_undo());
    CHECK(!document.can_redo());
    {
        const std::optional<Container::SVG> canvas = canvas_of(document);
        CHECK(canvas.has_value());
        if (canvas) {
            const std::optional<LengthPercentage> width = present_length_percentage(canvas->width);
            const std::optional<LengthPercentage> height = present_length_percentage(canvas->height);
            if (width) CHECK(to_text(*width) == "1024px");
            // the side that was left out keeps the default
            if (height) CHECK(to_text(*height) == "600");
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
    CHECK(document.undo_barred_by_io());
    document.redo();
    CHECK(document.find("after-reset") == kept);

    {
        Transaction edit = document.transaction();
        edit.append_child(document.root(), group("newer"));
        edit.commit();
    }
    CHECK(document.depth() == 2);

    const CommitId before_clear = document.commit_id();
    document.clear_history();
    CHECK(document.commit_id() == before_clear);
    CHECK(!document.commit_at(0));  // the baseline has no editable history
    CHECK(document.depth() == 0);
    CHECK(document.position() == 0);
    CHECK(!document.can_undo());
    CHECK(!document.undo_barred_by_io());
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

    // Undo moves the writable checkout. An empty transaction must preserve redo, and a
    // real edit after undo must branch without needing a snapshot to pin the old tail.
    {
        Document branch;
        branch.reset(Unset{}, Unset{});
        const Id branch_root = branch.root();
        const CommitId baseline = branch.commit_id();
        CHECK(baseline != 0);

        Id a = no_id;
        {
            Transaction edit = branch.transaction();
            a = edit.append_child(branch_root, group("a"));
            edit.commit(next_commit_id());
        }
        const CommitId after_a = branch.commit_id();
        CHECK(after_a > baseline);
        CHECK(branch.commit_at(0) == after_a);

        const TouchedNodes undone_a = branch.undo();
        CHECK(branch.find("a") == std::nullopt);
        CHECK(kind_of(undone_a, a) == TouchedKind::Remove);
        CHECK(branch.commit_id() == baseline);
        CHECK(branch.can_redo());

        // Nothing was written, so the hive keeps the redo tail.
        {
            Transaction empty = branch.transaction();
            empty.commit();
        }
        CHECK(branch.commit_id() == baseline);
        CHECK(branch.can_redo());
        const TouchedNodes redone_a = branch.redo();
        CHECK(kind_of(redone_a, a) == TouchedKind::Insert);
        CHECK(branch.commit_id() == after_a);
        CHECK(branch.find("a") == a);

        CHECK(branch.undo().size() != 0);
        Id b = no_id;
        {
            Transaction edit = branch.transaction();
            b = edit.append_child(branch_root, group("b"));
            edit.commit(next_commit_id());
        }
        CHECK(b != no_id);
        CHECK(b != a);  // ids stay monotonic; the abandoned id is not reused
        // An explicit id is not the discarded tail's id: a branch gets a fresh commit.
        const CommitId branch_commit = branch.commit_id();
        CHECK(branch_commit > after_a);
        CHECK(branch.commit_at(0) == branch_commit);
        CHECK(!branch.commit_at(1));
        CHECK(branch.find("b") == b);
        CHECK(branch.find("a") == std::nullopt);
        CHECK(!branch.can_redo());
    }

    // Checkout jumps straight to a commit on the current branch, in either direction,
    // and leaves the redo tail available.
    {
        Document document;
        document.reset(Unset{}, Unset{});
        const CommitId baseline = document.commit_id();

        Id a = no_id;
        {
            Transaction edit = document.transaction();
            a = edit.append_child(document.root(), group("a"));
            edit.commit(next_commit_id());
        }
        const CommitId after_a = document.commit_id();
        Id b = no_id;
        {
            Transaction edit = document.transaction();
            b = edit.append_child(document.root(), group("b"));
            edit.commit(next_commit_id());
        }
        const CommitId after_b = document.commit_id();
        CHECK(baseline < after_a);
        CHECK(after_a < after_b);

        const std::optional<HistoryCheckout> first = document.checkout(after_a);
        CHECK(first.has_value());
        if (first) {
            CHECK(first->position == 1);
            CHECK(kind_of(first->touched, b) == TouchedKind::Remove);
        }
        CHECK(document.commit_id() == after_a);
        CHECK(document.find("a") == a);
        CHECK(!document.find("b").has_value());
        CHECK(document.can_redo());

        const std::optional<HistoryCheckout> second = document.checkout(after_b);
        CHECK(second.has_value());
        if (second) {
            CHECK(second->position == 2);
            CHECK(kind_of(second->touched, b) == TouchedKind::Insert);
        }
        CHECK(document.commit_id() == after_b);
        CHECK(document.find("b") == b);

        const std::optional<HistoryCheckout> same = document.checkout(after_b);
        CHECK(same.has_value());
        if (same) {
            CHECK(same->position == 2);
            CHECK(same->no_effect);
            CHECK(same->touched.empty());
        }

        const std::optional<HistoryCheckout> floor = document.checkout(baseline);
        CHECK(floor.has_value());
        if (floor) CHECK(floor->position == 0);
        CHECK(document.commit_id() == baseline);
        CHECK(!document.find("a").has_value());
        CHECK(!document.find("b").has_value());
        CHECK(document.can_redo());

        CHECK(!document.checkout(after_b + 1000).has_value());
    }

    // One edit can span hives. The coordinator allocates one id and passes it to every
    // transaction, so MVCCHive's native commit id is the same checkout in each hive.
    {
        Document left;
        Document right;
        left.reset(Unset{}, Unset{});
        right.reset(Unset{}, Unset{});

        const CommitId shared = next_commit_id();
        {
            Transaction edit = left.transaction();
            edit.append_child(left.root(), group("left"));
            edit.commit(shared);
        }
        {
            Transaction edit = right.transaction();
            edit.append_child(right.root(), group("right"));
            edit.commit(shared);
        }

        CHECK(left.commit_id() == shared);
        CHECK(right.commit_id() == shared);

        left.undo();
        right.undo();
        CHECK(left.commit_id() != shared);
        CHECK(right.commit_id() != shared);
        left.redo();
        right.redo();
        CHECK(left.commit_id() == shared);
        CHECK(right.commit_id() == shared);

        // A hive with nothing to write still advances on an explicit empty commit, so
        // both checkouts can land on the same id.
        const CommitId aligned = next_commit_id();
        {
            Transaction edit = left.transaction();
            edit.commit(aligned);
        }
        {
            Transaction edit = right.transaction();
            edit.commit(aligned);
        }
        CHECK(left.commit_id() == aligned);
        CHECK(right.commit_id() == aligned);
        const std::size_t left_depth = left.depth();
        left.undo();
        right.undo();
        CHECK(left.commit_id() == shared);
        CHECK(right.commit_id() == shared);
        left.redo();
        right.redo();
        CHECK(left.commit_id() == aligned);
        CHECK(right.commit_id() == aligned);
        CHECK(left.depth() == left_depth);
    }

    if (failures != 0) {
        std::printf("document: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("document: all checks passed\n");
    return 0;
}
