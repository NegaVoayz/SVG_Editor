#include "TreeStore.hh"

#include <algorithm>
#include <type_traits>

namespace {

// The canvas a fresh document starts with, and what goes in for a side an `init` or
// a `canvas` left out. SVG's own default viewport is 300x150 CSS pixels; this
// editor's front end says 800x600 instead, so the number belongs to the document
// rather than to the command that asks for it.
inline const LengthPercentage fresh_canvas_width = Length{800.0F, Length::Unit::UserUnit};
inline const LengthPercentage fresh_canvas_height = Length{600.0F, Length::Unit::UserUnit};

// The payload of the root element. The root is part of the baseline, so erasing
// everything leaves an empty document rather than no document.
Node make_root_payload(std::optional<LengthPercentage> width, std::optional<LengthPercentage> height) {
    SVGNode element;
    element.identity_attr = IdentityAttr{"", ""};
    element.data = Container{ChildAttr{no_id}, Container::SVG{width ? std::move(width) : fresh_canvas_width,
                                                              height ? std::move(height) : fresh_canvas_height}};
    return Node{std::move(element)};
}

template<typename T>
bool has_row(const SwapDiffHive<T>& hive, Id row) {
    try {
        hive.at(row);
        return true;
    } catch (const std::out_of_range&) {
        return false;
    }
}

// Give an action every element payload of a node: the `data` alternative of an SVG element,
// and nothing for character data, which is a node with no element kind under it. The action
// then asks its own question with `if constexpr`, so no type is named here and no list of the
// kinds exists - the variant is the list.
template<typename Payload, typename Action>
void visit_data(Payload& payload, Action action) {
    std::visit(
        [action](auto& value) {
            if constexpr (Element<std::remove_cvref_t<decltype(value)>>) std::visit(action, value.data);
        },
        payload);
}

// The row that says who holds a node. Every alternative of Node carries one - that is what
// the static_assert next to the variant is for - so which alternative it is does not matter.
Id parent_link_of(const Node& payload) {
    return std::visit([](const auto& value) -> Id { return value.parent_link; }, payload);
}

}  // namespace

TreeStore::TreeStore() { reset(std::nullopt, std::nullopt); }

void TreeStore::reset(std::optional<LengthPercentage> width, std::optional<LengthPercentage> height) {
    if (in_transaction_) throw std::logic_error("TreeStore: the arena cannot be reset inside a transaction");

    // Forgetting everything means forgetting the slots too, so a reset document starts from
    // slot 0 again rather than from whatever was there before.
    nodes_.clear();
    parents_.clear();
    children_.clear();
    edges_.clear();

    // The root is the first payload, which is slot 0, because nothing is reserved in front of
    // it: a link that leads nowhere is no_id, not slot 0. Its rows come with it: every node is
    // held by something, only the root by nothing, and a child row if its type has one. They
    // are written as one transaction under the baseline's diff id, and that history is dropped
    // below, so what is left is a baseline.
    begin(1);
    Node payload = make_root_payload(std::move(width), std::move(height));
    set_parent_link(payload, parents_.insert(ParentLink{no_id}));
    make_child_row(payload);
    root_ = nodes_.insert(std::move(payload));
    commit();

    // The root is the baseline, not an edit: dropping it from the history is what makes
    // "undo everything" land on an empty document, and it leaves diff id 1 free for the first
    // real transaction.
    clear_history();
}

void TreeStore::begin(Id diff_id) {
    if (in_transaction_) throw std::logic_error("TreeStore: a transaction is already open");
    nodes_.begin_transaction(diff_id);
    parents_.begin_transaction(diff_id);
    children_.begin_transaction(diff_id);
    edges_.begin_transaction(diff_id);
    in_transaction_ = true;
}

void TreeStore::commit() {
    require_transaction();
    in_transaction_ = false;
    nodes_.commit();
    parents_.commit();
    children_.commit();
    edges_.commit();
}

void TreeStore::rollback() {
    require_transaction();
    in_transaction_ = false;
    nodes_.rollback();
    parents_.rollback();
    children_.rollback();
    edges_.rollback();
}

void TreeStore::seek(Id diff_id) {
    if (in_transaction_) throw std::logic_error("TreeStore: the history cannot be walked inside a transaction");
    nodes_.seek(diff_id);
    parents_.seek(diff_id);
    children_.seek(diff_id);
    edges_.seek(diff_id);
}

void TreeStore::clear_history() {
    if (in_transaction_) throw std::logic_error("TreeStore: the history cannot be dropped inside a transaction");
    nodes_.clear_history();
    parents_.clear_history();
    children_.clear_history();
    edges_.clear_history();
}

void TreeStore::require_transaction() const {
    if (!in_transaction_) throw std::logic_error("TreeStore: this edit needs an open transaction");
}

bool TreeStore::has_node(Id node) const { return has_row(nodes_, node); }

const Node& TreeStore::record(Id node) const {
    return nodes_.at(node);  // throws std::out_of_range when no node lives there
}

bool TreeStore::has_edge(Id edge) const { return has_row(edges_, edge); }

Id TreeStore::parent_of(Id node) const { return parents_.at(parent_link_of(record(node))).parent; }

Id TreeStore::last_edge_of(Id node) const {
    const Id row = child_row_of(record(node));
    if (row == no_id) return no_id;  // the payload cannot hold children, so it has no chain
    return children_.at(row).last_child_edge;
}

bool TreeStore::can_hold(Id node) const { return child_row_of(record(node)) != no_id; }

// The row a payload keeps its child chain in, or no_id when it has nowhere to keep one. The
// question is the member: a payload that has a child link answers with the handle in it, and
// one that has not cannot hold children at all.
Id TreeStore::child_row_of(const Node& payload) const {
    Id row = no_id;
    visit_data(payload, [&row](const auto& data) {
        if constexpr (ChildHolder<std::remove_cvref_t<decltype(data)>>) row = data.child_attr.links;
    });
    return row;
}

// The row a new payload keeps its child chain in, made now, with its handle written into the
// link itself. That is the whole of the capability: the type has the storage, and this is the
// store that uses it.
void TreeStore::make_child_row(Node& payload) {
    visit_data(payload, [this](auto& data) {
        if constexpr (ChildHolder<std::remove_cvref_t<decltype(data)>>) {
            data.child_attr.links = children_.insert(ChildLink{no_id});
        }
    });
}

// Put an existing row into a payload that has somewhere for it, and say whether it did.
bool TreeStore::set_child_row(Node& payload, Id row) {
    bool placed = false;
    visit_data(payload, [&placed, row](auto& data) {
        if constexpr (ChildHolder<std::remove_cvref_t<decltype(data)>>) {
            data.child_attr.links = row;
            placed = true;
        }
    });
    return placed;
}

// Every node carries the handle of the row that says who holds it; the store gives it one
// when the node is added, and a replacement payload takes over the node's own.
void TreeStore::set_parent_link(Node& payload, Id row) {
    std::visit([row](auto& value) { value.parent_link = row; }, payload);
}

Id TreeStore::parent(Id node) const {
    if (!has_node(node)) return no_id;
    return parent_of(node);
}

std::vector<Id> TreeStore::children(Id parent) const {
    std::vector<Id> result;
    if (!has_node(parent)) return result;

    // The chain runs from the last child backwards, so collect and flip.
    for (Id edge = last_edge_of(parent); edge != no_id; edge = edges_.at(edge).prev) {
        result.push_back(edges_.at(edge).child_id);
    }
    std::reverse(result.begin(), result.end());
    return result;
}

Id TreeStore::append_child(Id parent, Node payload) {
    return insert_at(parent, no_id, std::move(payload));
}

void TreeStore::attach_last(Id parent, Id node) {
    attach_at(parent, no_id, node);
}

Id TreeStore::insert_at(Id parent, Id before, Node payload) {
    require_transaction();
    if (!can_hold(parent)) {
        throw std::logic_error("TreeStore: this node cannot hold children");
    }

    // The rows the node owns, before the node itself: every node is held by something, and a
    // payload with a child link gets a row for its own chain, which the link keeps.
    const Id parent_row = parents_.insert(ParentLink{parent});
    set_parent_link(payload, parent_row);
    make_child_row(payload);
    const Id child = nodes_.insert(std::move(payload));
    link_before(parent, before, child);
    return child;
}

void TreeStore::attach_at(Id parent, Id before, Id node) {
    require_transaction();
    if (node == root_) throw std::logic_error("TreeStore: the root cannot be attached to anything");
    if (parent == node) throw std::logic_error("TreeStore: a node cannot hold itself");
    if (before == node) throw std::logic_error("TreeStore: a node cannot be placed in front of itself");
    if (!can_hold(parent)) {
        throw std::logic_error("TreeStore: this node cannot hold children");
    }

    // The place a node moves to must not be inside the node itself: the parent
    // chain of the new parent is what would have to contain it.
    for (Id walk = parent; walk != no_id; walk = parent_of(walk)) {
        if (walk == node) throw std::logic_error("TreeStore: a node cannot be moved inside itself");
    }

    // The move itself is small: the sibling links around the node, and the node's own parent
    // row - which is not rewritten when the node only changes place among its siblings. The
    // payload never changes and is never copied.
    const Id previous_parent = parent_of(node);
    if (previous_parent != no_id) unlink(node);
    link_before(parent, before, node);
    if (previous_parent != parent) patch_parent(node, parent);
}

void TreeStore::update(Id node, Node payload) {
    // Both rows belong to the node, not to the payload that happens to be in it, so a
    // replacement takes the node's handles - a payload copied from another node cannot bring
    // that node's place with it. The type still has to agree: an update does not turn a group
    // into a rect, because then the child row and the payload would no longer say the same
    // thing.
    const Node& stored = record(node);
    const Id row = child_row_of(stored);
    if (set_child_row(payload, row) != (row != no_id)) {
        throw std::logic_error("TreeStore: an update cannot change whether a node holds children");
    }
    set_parent_link(payload, parent_link_of(stored));
    patch_row(nodes_, node, [&payload](Node& record) { record = std::move(payload); });
}

void TreeStore::erase_subtree(Id node) {
    require_transaction();
    if (node == root_) throw std::logic_error("TreeStore: the root cannot be erased");

    // Children first: their links are patched while their parent is still there.
    for (const Id child : children(node)) erase_subtree(child);
    unlink(node);

    // The node goes, and the rows that were its: the parent row every node has, and the child
    // row its payload keeps the handle of. Read them before the record is gone.
    const Node stored = record(node);
    const Id child_row = child_row_of(stored);
    parents_.remove(parent_link_of(stored));
    if (child_row != no_id) children_.remove(child_row);
    nodes_.remove(node);
}

void TreeStore::patch_parent(Id node, Id parent) {
    patch_row(parents_, parent_link_of(record(node)), [parent](ParentLink& row) { row.parent = parent; });
}

void TreeStore::patch_last_edge(Id node, Id edge) {
    patch_row(children_, child_row_of(record(node)), [edge](ChildLink& row) { row.last_child_edge = edge; });
}

void TreeStore::link_last(Id parent, Id child) {
    // Read what the op needs before it starts moving rows around.
    const Id previous_last_edge = last_edge_of(parent);
    const Id edge = edges_.insert(Edge{child, previous_last_edge});
    patch_last_edge(parent, edge);
}

// The link that holds one child under its parent. The chain runs from the last child
// backwards, so this reads the whole list; it is asked once per placed node, and a
// list is as long as the element has children.
Id TreeStore::edge_of(Id parent, Id child) const {
    for (Id edge = last_edge_of(parent); edge != no_id; edge = edges_.at(edge).prev) {
        if (edges_.at(edge).child_id == child) return edge;
    }
    throw std::invalid_argument("TreeStore: the node to place in front of is not a child of that parent");
}

// Puts a node in front of a sibling in the link chain. The node must not be linked
// yet: there is one link per child, so the caller takes the old one away first.
//
// Inserting in front of a sibling is the same splice wherever the sibling sits,
// because the chain runs backwards: the new link takes the anchor's place in the
// backwards walk and points at what the anchor pointed at. Only the end is cheaper,
// and that is what append_child() asks for.
void TreeStore::link_before(Id parent, Id before, Id child) {
    if (before == no_id) {
        link_last(parent, child);
        return;
    }
    // The anchor keeps its own link slot - patching what that link points at is
    // what moves the new node in front of it - and the node's own link is fresh.
    const Id anchor = edge_of(parent, before);
    const Id previous = edges_.at(anchor).prev;
    const Id placed = edges_.insert(Edge{child, previous});
    patch_edge(anchor, Edge{before, placed});
}

void TreeStore::unlink(Id node) {
    const Id parent = parent_of(node);
    if (parent == no_id) throw std::logic_error("TreeStore: this node is not linked under anything");

    // Walk the parent's chain for the edge that holds this node, keeping the edge
    // that points at it, which is the one that has to be spliced.
    Id closer = no_id;
    for (Id edge = last_edge_of(parent); edge != no_id; ) {
        const Edge link = edges_.at(edge);  // a copy: the walk continues after the edits
        if (link.child_id == node) {
            if (closer == no_id) {
                // It was the last child, so the parent's chain now ends at the one before it.
                patch_last_edge(parent, link.prev);
            } else {
                patch_edge(closer, Edge{edges_.at(closer).child_id, link.prev});
            }
            edges_.remove(edge);
            return;
        }
        closer = edge;
        edge = link.prev;
    }
    throw std::logic_error("TreeStore: the node is not linked into its parent");
}

void TreeStore::patch_edge(Id edge, Edge replacement) {
    if (!has_edge(edge)) throw std::out_of_range("TreeStore: no link at this handle");
    patch_row(edges_, edge, [&replacement](Edge& row) { row = replacement; });
}
