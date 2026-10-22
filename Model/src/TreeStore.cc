#include "TreeStore.hh"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

using Edit = TreeStore::Hive::Transaction;
using NodeRef = std::reference_wrapper<const Node>;

// The canvas a fresh document starts with, and what goes in for a side an `init` or
// a `canvas` left out. SVG's own default viewport is 300x150 CSS pixels; this
// editor's front end says 800x600 instead, so the number belongs to the document
// rather than to the command that asks for it.
inline const LengthPercentage fresh_canvas_width = Length{800.0F, Length::Unit::UserUnit};
inline const LengthPercentage fresh_canvas_height = Length{600.0F, Length::Unit::UserUnit};

struct Links {
    Id parent;
    Id previous;
    Id next;
};

// Give an action every element payload of a node: the `data` alternative of an SVG
// element, and nothing for character data. The action then asks its own question with
// `if constexpr`, so no kind is named here and no list of them exists - the variant is
// the list.
template<typename Payload, typename Action>
void visit_data(Payload& payload, Action action) {
    std::visit(
        [action](auto& value) {
            using PayloadType = std::remove_cvref_t<decltype(value)>;
            if constexpr (Element<PayloadType>) {
                std::visit(action, value.data);
            } else {
                static_assert(std::is_same_v<PayloadType, PlainText>,
                              "a non-element node payload has no data alternatives");
            }
        },
        payload);
}

Links links_of(const Node& node) {
    return std::visit(
        [](const auto& value) {
            return Links{value.parent_id, value.previous_sibling, value.next_sibling};
        },
        node);
}

void set_links(Node& node, Links links) {
    std::visit(
        [links](auto& value) {
            value.parent_id = links.parent;
            value.previous_sibling = links.previous;
            value.next_sibling = links.next;
        },
        node);
}

std::optional<NodeRef> staged_node(Edit& edit, Id id) {
    const auto found = edit.find(id);
    if (!found) return std::nullopt;
    return *found;
}

NodeRef require_staged(Edit& edit, Id id) {
    const std::optional<NodeRef> found = staged_node(edit, id);
    if (!found) throw std::out_of_range("TreeStore: no live node at this id");
    return *found;
}

Links links_in(Edit& edit, Id id) { return links_of(require_staged(edit, id).get()); }

std::optional<ChildAttr> child_attr_in(Edit& edit, Id id) {
    return child_attr_of(require_staged(edit, id).get());
}

// The payload of the root element. The root is part of the baseline, so erasing
// everything leaves an empty document rather than no document.
Node make_root_payload(std::optional<LengthPercentage> width, std::optional<LengthPercentage> height) {
    SVGNode element;
    element.identity_attr = IdentityAttr{"", ""};
    element.data = Container{ChildAttr{no_id, no_id},
                             Container::SVG{width ? std::move(width) : fresh_canvas_width,
                                            height ? std::move(height) : fresh_canvas_height}};
    return Node{std::move(element)};
}

}  // namespace

std::optional<ChildAttr> child_attr_of(const Node& node) {
    std::optional<ChildAttr> result;
    visit_data(
        node,
        [&result](const auto& value) {
            if constexpr (ChildHolder<std::remove_cvref_t<decltype(value)>>) result = value.child_attr;
        });
    return result;
}

bool set_child_attr(Node& node, const ChildAttr& attr) {
    bool placed = false;
    visit_data(
        node,
        [&placed, &attr](auto& value) {
            if constexpr (ChildHolder<std::remove_cvref_t<decltype(value)>>) {
                value.child_attr = attr;
                placed = true;
            }
        });
    return placed;
}

TreeStore::TreeStore() = default;

void TreeStore::reset(std::optional<LengthPercentage> width, std::optional<LengthPercentage> height) {
    // Forgetting everything means releasing the current snapshot too: its guard would
    // keep the old generation alive, and none of it can be reached from this document.
    view_.reset();
    nodes_.clear();
    checkouts_.clear();
    position_ = 0;

    // The root is the first virtual id, 0, because no id is reserved in front of it: a
    // link that leads nowhere is no_id. Its history is dropped immediately, which makes
    // it the baseline and leaves the next id for the document's first edit.
    auto edit = nodes_.begin_transaction();
    root_ = edit.emplace(make_root_payload(std::move(width), std::move(height)));
    static_cast<void>(edit.commit(next_commit_id()));
    nodes_.clear_history();
    view_ = nodes_.head();
    checkouts_.push_back(view_->commit_id());
    require_checkout_matches_view();
}

void TreeStore::reset_empty() {
    view_.reset();
    nodes_.clear();
    checkouts_.clear();
    position_ = 0;
    root_ = no_id;
}

TreeStore::Edit TreeStore::begin() {
    if (!view_) throw std::logic_error("TreeStore: no document");
    return nodes_.begin_transaction();
}

TreeStore::Edit TreeStore::begin_load() { return nodes_.begin_transaction(); }

void TreeStore::commit(Edit& edit, CommitId commit) {
    const Hive::CommitId before = view_ ? view_->commit_id() : 0;
    const Snapshot next = commit == 0 ? edit.commit() : edit.commit(commit);
    view_ = next;
    if (next.commit_id() == before) return;  // an edit that wrote nothing is no block

    observe_commit_id(next.commit_id());

    // The new edit replaces whatever redo tail the checkout had left.
    if (checkouts_.empty()) {
        checkouts_.push_back(next.commit_id());
        position_ = 0;
        require_checkout_matches_view();
        return;
    }
    checkouts_.resize(position_ + 1);
    checkouts_.push_back(next.commit_id());
    position_ = checkouts_.size() - 1;
    require_checkout_matches_view();
}

void TreeStore::rollback(Edit& edit) noexcept {
    edit.rollback();
}

bool TreeStore::can_undo() const { return position_ > 0; }
bool TreeStore::can_redo() const { return position_ + 1 < checkouts_.size(); }

TouchedNodes TreeStore::touched_by(Hive::CommitId commit, Direction direction) const {
    const std::vector<Hive::Diff> diffs = nodes_.diffs(commit);
    TouchedNodes touched;
    touched.reserve(diffs.size());
    for (const Hive::Diff& diff : diffs) {
        TouchedKind kind = TouchedKind::Update;
        switch (diff.kind) {
            case Hive::DiffKind::insert: kind = TouchedKind::Insert; break;
            case Hive::DiffKind::update: kind = TouchedKind::Update; break;
            case Hive::DiffKind::remove: kind = TouchedKind::Remove; break;
        }
        if (direction == Direction::Undo) {
            if (kind == TouchedKind::Insert) kind = TouchedKind::Remove;
            else if (kind == TouchedKind::Remove) kind = TouchedKind::Insert;
        }
        touched.push_back(TouchedNode{static_cast<Id>(diff.virtual_id), kind});
    }
    return touched;
}

TouchedNodes TreeStore::undo() {
    if (!can_undo()) return {};
    const Hive::CommitId commit = view_->commit_id();
    if (!nodes_.undo()) return {};
    --position_;
    view_ = nodes_.head();
    require_checkout_matches_view();
    return touched_by(commit, Direction::Undo);
}

TouchedNodes TreeStore::redo() {
    if (!can_redo()) return {};
    if (!nodes_.redo()) return {};
    ++position_;
    view_ = nodes_.head();
    require_checkout_matches_view();
    return touched_by(view_->commit_id(), Direction::Redo);
}

std::size_t TreeStore::depth() const { return checkouts_.empty() ? 0 : checkouts_.size() - 1; }
std::size_t TreeStore::position() const { return position_; }

CommitId TreeStore::commit_id() const { return view_ ? view_->commit_id() : 0; }
CommitId TreeStore::baseline_commit() const { return checkouts_.empty() ? 0 : checkouts_.front(); }

std::optional<CommitId> TreeStore::commit_at(std::size_t position) const {
    if (checkouts_.size() < 2 || position >= checkouts_.size() - 1) return std::nullopt;
    return checkouts_[position + 1];
}

std::optional<HistoryCheckout> TreeStore::checkout(CommitId commit) {
    if (!view_) return std::nullopt;
    const auto found = std::find(checkouts_.begin(), checkouts_.end(), commit);
    if (found == checkouts_.end()) return std::nullopt;
    const std::size_t target = static_cast<std::size_t>(std::distance(checkouts_.begin(), found));
    if (target == position_) return HistoryCheckout{position_, {}, true};

    // The hive only exposes undo/redo as ways to move its checkout, so a jump walks the
    // retained branch once. The work is the sum of the commits crossed, not a run of
    // separate user instructions, and the redo tail is left intact.
    TouchedNodes touched;
    auto record = [&touched](const TouchedNodes& steps) {
        for (const TouchedNode& step : steps) {
            const auto existing =
                std::find_if(touched.begin(), touched.end(), [&step](const TouchedNode& value) {
                    return value.node == step.node;
                });
            if (existing == touched.end()) {
                touched.push_back(step);
            } else {
                existing->kind = step.kind;
            }
        }
    };

    if (target < position_) {
        while (position_ > target) {
            const CommitId leaving = view_->commit_id();
            if (!nodes_.undo()) throw std::logic_error("TreeStore: checkout left the retained history");
            --position_;
            view_ = nodes_.head();
            record(touched_by(leaving, Direction::Undo));
        }
    } else {
        while (position_ < target) {
            if (!nodes_.redo()) throw std::logic_error("TreeStore: checkout left the retained history");
            ++position_;
            view_ = nodes_.head();
            record(touched_by(view_->commit_id(), Direction::Redo));
        }
    }
    require_checkout_matches_view();
    return HistoryCheckout{position_, std::move(touched), false};
}

void TreeStore::require_checkout_matches_view() const {
    if (!view_ || checkouts_.empty() || position_ >= checkouts_.size()) return;
    if (view_->commit_id() != checkouts_[position_]) {
        throw std::logic_error("TreeStore: the hive checkout and the recorded commit disagree");
    }
}

void TreeStore::clear_history() {
    if (!view_) return;
    const CommitId current = checkouts_.empty() ? view_->commit_id() : checkouts_[position_];
    view_.reset();
    nodes_.clear_history();
    checkouts_.clear();
    checkouts_.push_back(current);
    position_ = 0;
    view_ = nodes_.head();
    require_checkout_matches_view();
}

bool TreeStore::has_node(Id node) const { return live_node(node).has_value(); }

const Node& TreeStore::record(Id node) const {
    const std::optional<NodeRef> found = live_node(node);
    if (!found) throw std::out_of_range("TreeStore: no live node at this id");
    return found->get();
}

std::optional<TreeStore::NodeRef> TreeStore::live_node(Id node) const {
    if (!view_) return std::nullopt;
    const auto found = view_->find(node);
    if (!found) return std::nullopt;
    return *found;
}

Id TreeStore::parent(Id node) const {
    const std::optional<NodeRef> found = live_node(node);
    if (!found) return no_id;
    return links_of(found->get()).parent;
}

std::vector<Id> TreeStore::children(Id parent) const {
    std::vector<Id> result;
    const std::optional<NodeRef> held = live_node(parent);
    if (!held) return result;

    const std::optional<ChildAttr> attr = child_attr_of(held->get());
    if (!attr) return result;

    for (Id child = attr->first_child; child != no_id;) {
        const std::optional<NodeRef> node = live_node(child);
        if (!node) throw std::logic_error("TreeStore: a child link names no live node");
        result.push_back(child);
        child = links_of(node->get()).next;
    }
    return result;
}

Id TreeStore::create_root(Edit& edit, Node payload) {
    if (root_ != no_id) throw std::logic_error("TreeStore: the document already has a root");
    root_ = edit.emplace(std::move(payload));
    return root_;
}

Id TreeStore::append_child(Edit& edit, Id parent, Node payload) {
    return insert_at(edit, parent, no_id, std::move(payload));
}

void TreeStore::attach_last(Edit& edit, Id parent, Id node) {
    attach_at(edit, parent, no_id, node);
}

Id TreeStore::insert_at(Edit& edit, Id parent, Id before, Node payload) {
    const NodeRef holder = require_staged(edit, parent);
    const std::optional<ChildAttr> attr = child_attr_of(holder.get());
    if (!attr) throw std::logic_error("TreeStore: this node cannot hold children");

    Id previous = attr->last_child;
    if (before != no_id) {
        const Links anchor = links_in(edit, before);
        if (anchor.parent != parent) {
            throw std::invalid_argument("TreeStore: the node to place in front of is not a child of that parent");
        }
        previous = anchor.previous;
    }

    set_links(payload, Links{parent, previous, before});
    const Id child = edit.emplace(std::move(payload));

    if (previous == no_id && before == no_id) {
        patch_child_attr(edit, parent, [child](ChildAttr& ends) {
            ends.first_child = child;
            ends.last_child = child;
        });
    } else {
        if (previous != no_id) {
            patch_node(edit, previous, [child](Node& node) {
                Links links = links_of(node);
                links.next = child;
                set_links(node, links);
            });
        } else {
            patch_child_attr(edit, parent, [child](ChildAttr& ends) { ends.first_child = child; });
        }

        if (before != no_id) {
            patch_node(edit, before, [child](Node& node) {
                Links links = links_of(node);
                links.previous = child;
                set_links(node, links);
            });
        } else {
            patch_child_attr(edit, parent, [child](ChildAttr& ends) { ends.last_child = child; });
        }
    }
    return child;
}

void TreeStore::attach_at(Edit& edit, Id parent, Id before, Id node) {
    if (node == root_) throw std::logic_error("TreeStore: the root cannot be attached to anything");
    if (parent == node) throw std::logic_error("TreeStore: a node cannot hold itself");
    if (before == node) throw std::logic_error("TreeStore: a node cannot be placed in front of itself");

    const NodeRef holder = require_staged(edit, parent);
    if (!child_attr_of(holder.get())) throw std::logic_error("TreeStore: this node cannot hold children");
    static_cast<void>(require_staged(edit, node));

    // The place a node moves to must not be inside the node itself: the parent
    // chain of the new parent is what would have to contain it.
    for (Id walk = parent; walk != no_id; walk = links_in(edit, walk).parent) {
        if (walk == node) throw std::logic_error("TreeStore: a node cannot be moved inside itself");
    }

    if (before != no_id) {
        const Links anchor = links_in(edit, before);
        if (anchor.parent != parent) {
            throw std::invalid_argument("TreeStore: the node to place in front of is not a child of that parent");
        }
    }

    detach(edit, node);

    // Read the target after the detach: if the node came from the same level, removing
    // it changed either the parent's last child or the anchor's previous link.
    const std::optional<ChildAttr> ends = child_attr_in(edit, parent);
    Id previous = ends->last_child;
    if (before != no_id) previous = links_in(edit, before).previous;

    patch_node(edit, node, [parent, previous, before](Node& record) {
        set_links(record, Links{parent, previous, before});
    });

    if (previous == no_id && before == no_id) {
        patch_child_attr(edit, parent, [node](ChildAttr& attr) {
            attr.first_child = node;
            attr.last_child = node;
        });
    } else {
        if (previous != no_id) {
            patch_node(edit, previous, [node](Node& record) {
                Links links = links_of(record);
                links.next = node;
                set_links(record, links);
            });
        } else {
            patch_child_attr(edit, parent, [node](ChildAttr& attr) { attr.first_child = node; });
        }

        if (before != no_id) {
            patch_node(edit, before, [node](Node& record) {
                Links links = links_of(record);
                links.previous = node;
                set_links(record, links);
            });
        } else {
            patch_child_attr(edit, parent, [node](ChildAttr& attr) { attr.last_child = node; });
        }
    }
}

void TreeStore::detach(Edit& edit, Id node) {
    const Links link = links_in(edit, node);
    if (link.parent == no_id) throw std::logic_error("TreeStore: this node is not linked under anything");

    if (link.previous == no_id && link.next == no_id) {
        patch_child_attr(edit, link.parent, [](ChildAttr& attr) {
            attr.first_child = no_id;
            attr.last_child = no_id;
        });
    } else {
        if (link.previous != no_id) {
            patch_node(edit, link.previous, [next = link.next](Node& record) {
                Links links = links_of(record);
                links.next = next;
                set_links(record, links);
            });
        } else {
            patch_child_attr(edit, link.parent, [next = link.next](ChildAttr& attr) { attr.first_child = next; });
        }

        if (link.next != no_id) {
            patch_node(edit, link.next, [previous = link.previous](Node& record) {
                Links links = links_of(record);
                links.previous = previous;
                set_links(record, links);
            });
        } else {
            patch_child_attr(edit, link.parent, [previous = link.previous](ChildAttr& attr) { attr.last_child = previous; });
        }
    }
}

void TreeStore::update(Edit& edit, Id node, Node payload) {
    const NodeRef stored = require_staged(edit, node);

    const std::optional<ChildAttr> stored_children = child_attr_of(stored.get());
    const std::optional<ChildAttr> new_children = child_attr_of(payload);
    if (stored_children.has_value() != new_children.has_value()) {
        throw std::logic_error("TreeStore: an update cannot change whether a node holds children");
    }

    // The node's place and child list belong to the node, not to the payload that
    // happens to be in it.
    set_links(payload, links_of(stored.get()));
    if (stored_children) set_child_attr(payload, *stored_children);
    edit.replace(node, std::move(payload));
}

void TreeStore::erase_subtree(Edit& edit, Id node) {
    if (node == root_) throw std::logic_error("TreeStore: the root cannot be erased");
    static_cast<void>(require_staged(edit, node));

    // The node leaves its level first; its descendants keep their own links until they
    // are replaced, which is what lets the walk read them as one tree.
    detach(edit, node);

    std::vector<Id> pending{node};
    std::vector<Id> doomed;
    while (!pending.empty()) {
        const Id current = pending.back();
        pending.pop_back();

        const NodeRef found = require_staged(edit, current);
        doomed.push_back(current);
        if (const std::optional<ChildAttr> children = child_attr_of(found.get())) {
            for (Id child = children->first_child; child != no_id;) {
                const NodeRef child_node = require_staged(edit, child);
                pending.push_back(child);
                child = links_of(child_node.get()).next;
            }
        }
    }

    for (const Id id : doomed) edit.erase(id);
}
