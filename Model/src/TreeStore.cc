#include "TreeStore.hh"

namespace {

// The payload of the root element. The root is part of the baseline, so erasing
// everything leaves an empty document rather than no document.
Node make_root_payload() {
    SVGNode element;
    element.identity_attr = IdentityAttr{"", "", no_id};
    element.data = Container{ChildAttr{no_id}, Container::SVG{}};
    return Node{std::move(element)};
}

// Only the nodes SVG lets hold children may grow a child link: a container or a
// text element, both of which carry the ChildAttr the links belong to. Without
// this, a shape could end up with children that no one can ever write out.
bool can_hold_children(const Node& payload) {
    if (std::holds_alternative<PlainText>(payload)) return false;
    return std::holds_alternative<Container>(std::get<SVGNode>(payload).data) ||
           std::holds_alternative<Text>(std::get<SVGNode>(payload).data);
}

}  // namespace

TreeStore::TreeStore() {
    // The root is the first payload, which is slot 0, because nothing is reserved
    // in front of it: a link that leads nowhere is no_id, not slot 0.
    root_ = nodes_.insert(1, NodeRecord{make_root_payload(), no_id, no_id});

    // The root is the baseline, not an edit: dropping it from the history is what
    // makes "undo everything" land on an empty document, and it leaves diff id 1
    // free for the first real transaction.
    nodes_.clear_history();
}

void TreeStore::begin(Id diff_id) {
    if (in_transaction_) throw std::logic_error("TreeStore: a transaction is already open");
    nodes_.begin_transaction(diff_id);
    edges_.begin_transaction(diff_id);
    in_transaction_ = true;
}

void TreeStore::commit() {
    require_transaction();
    in_transaction_ = false;
    nodes_.commit();
    edges_.commit();
}

void TreeStore::rollback() {
    require_transaction();
    in_transaction_ = false;
    nodes_.rollback();
    edges_.rollback();
}

void TreeStore::seek(Id diff_id) {
    if (in_transaction_) throw std::logic_error("TreeStore: the history cannot be walked inside a transaction");
    nodes_.seek(diff_id);
    edges_.seek(diff_id);
}

void TreeStore::require_transaction() const {
    if (!in_transaction_) throw std::logic_error("TreeStore: this edit needs an open transaction");
}

bool TreeStore::has_node(Id node) const {
    try {
        nodes_.at(node);
        return true;
    } catch (const std::out_of_range&) {
        return false;
    }
}

const NodeRecord& TreeStore::record(Id node) const {
    return nodes_.at(node);  // throws std::out_of_range when no node lives there
}

bool TreeStore::has_edge(Id edge) const {
    try {
        edges_.at(edge);
        return true;
    } catch (const std::out_of_range&) {
        return false;
    }
}

std::vector<Id> TreeStore::children(Id parent) const {
    std::vector<Id> result;
    if (!has_node(parent)) return result;

    // The chain runs from the last child backwards, so collect and flip.
    for (Id edge = record(parent).last_child_edge; edge != no_id; edge = edges_.at(edge).prev) {
        result.push_back(edges_.at(edge).child_id);
    }
    std::reverse(result.begin(), result.end());
    return result;
}

Id TreeStore::append_child(Id parent, Node payload) {
    require_transaction();
    if (!can_hold_children(record(parent).payload)) {
        throw std::logic_error("TreeStore: this node cannot hold children");
    }
    const Id child = nodes_.insert(NodeRecord{std::move(payload), parent, no_id});
    link_last(parent, child);
    return child;
}

void TreeStore::attach_last(Id parent, Id node) {
    require_transaction();
    if (node == root_) throw std::logic_error("TreeStore: the root cannot be attached to anything");
    if (parent == node) throw std::logic_error("TreeStore: a node cannot hold itself");
    if (!can_hold_children(record(parent).payload)) {
        throw std::logic_error("TreeStore: this node cannot hold children");
    }

    // The place a node moves to must not be inside the node itself: the parent
    // chain of the new parent is what would have to contain it.
    for (Id walk = parent; walk != no_id; walk = record(walk).parent) {
        if (walk == node) throw std::logic_error("TreeStore: a node cannot be moved inside itself");
    }

    if (record(node).parent != no_id) unlink(node);
    link_last(parent, node);
    patch_node(node, [parent](NodeRecord& record) { record.parent = parent; });
}

void TreeStore::update(Id node, Node payload) {
    require_transaction();
    patch_node(node, [&payload](NodeRecord& record) { record.payload = std::move(payload); });
}

void TreeStore::erase_subtree(Id node) {
    require_transaction();
    if (node == root_) throw std::logic_error("TreeStore: the root cannot be erased");

    // Children first: their links are patched while their parent is still there.
    for (const Id child : children(node)) erase_subtree(child);
    unlink(node);
    nodes_.remove(node);
}

void TreeStore::link_last(Id parent, Id child) {
    // Read what the op needs before it starts moving payloads around.
    const Id previous_last_edge = record(parent).last_child_edge;
    const Id edge = edges_.insert(Edge{child, previous_last_edge});
    patch_node(parent, [edge](NodeRecord& record) { record.last_child_edge = edge; });
}

void TreeStore::unlink(Id node) {
    const Id parent = record(node).parent;
    if (parent == no_id) throw std::logic_error("TreeStore: this node is not linked under anything");

    // Walk the parent's chain for the edge that holds this node, keeping the edge
    // that points at it, which is the one that has to be spliced.
    Id closer = no_id;
    for (Id edge = record(parent).last_child_edge; edge != no_id; ) {
        const Edge link = edges_.at(edge);  // a copy: the walk continues after the edits
        if (link.child_id == node) {
            if (closer == no_id) {
                // It was the last child, so the parent now ends at the one before it.
                patch_node(parent, [prev = link.prev](NodeRecord& record) { record.last_child_edge = prev; });
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
    require_transaction();
    if (!has_edge(edge)) throw std::out_of_range("TreeStore: no link at this handle");

    const Id scratch = edges_.insert(std::move(replacement));
    edges_.swap(edge, scratch);
    edges_.remove(scratch);
}
