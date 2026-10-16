#include "Document.hh"

#include <stdexcept>
#include <utility>

#include "FileSystem.hh"
#include "TreeStore.hh"

namespace {

// TODO: the missing half of the Model. The serializer walks the tree and writes
// SVG source; until it exists save() can only report that much.
Status serialize(const TreeStore& store, std::string& out_svg) {
    static_cast<void>(store);
    static_cast<void>(out_svg);
    return Status{false, "the SVG serializer is not implemented yet"};
}

// The hive reads diff id 0 as "before everything that is left": no diff id is
// ever 0, which is what leaves it free for that meaning.
constexpr Id before_everything = 0;

}  // namespace

// The hive calls the end of a link chain npos and Model calls it no_id: one value
// under two names, because the public headers cannot see the dependency.
static_assert(no_id == npos);

Document::Document(Document&&) noexcept = default;
Document& Document::operator=(Document&&) noexcept = default;

Id Document::root() const { return store_.root(); }

std::optional<Node> Document::node(Id id) const {
    if (!store_.has_node(id)) return std::nullopt;
    return store_.record(id).payload;
}

std::vector<Id> Document::children(Id parent) const { return store_.children(parent); }

std::optional<Id> Document::parent(Id node) const {
    if (!store_.has_node(node)) return std::nullopt;
    const Id above = store_.record(node).parent;
    if (above == no_id) return std::nullopt;
    return above;
}

std::optional<Id> Document::find(std::string_view svg_id) const {
    if (svg_id.empty()) return std::nullopt;

    // No index yet: a walk cannot go stale, and the tree is small. The arena does
    // hand out no way to list its slots, so the tree is the only index there is.
    std::vector<Id> pending{store_.root()};
    while (!pending.empty()) {
        const Id current = pending.back();
        pending.pop_back();
        if (!store_.has_node(current)) continue;
        if (const Node& payload = store_.record(current).payload;
            std::holds_alternative<SVGNode>(payload) &&
            std::get<SVGNode>(payload).identity_attr.id == svg_id) {
            return current;
        }
        for (const Id child : store_.children(current)) pending.push_back(child);
    }
    return std::nullopt;
}

Transaction Document::transaction() { return Transaction{*this}; }

bool Document::can_undo() const { return applied_ > 0; }
bool Document::can_redo() const { return applied_ < blocks_.size(); }
void Document::undo() { if (can_undo()) seek_to(applied_ - 1); }
void Document::redo() { if (can_redo()) seek_to(applied_ + 1); }
std::size_t Document::depth() const { return blocks_.size(); }
std::size_t Document::position() const { return applied_; }

// Walk both hives to the end of the block with this id. Every hive applies only
// the diffs that were written to it, so the step has to be a seek with an exact id
// instead of one block back per hive: a transaction that touched only one of the
// two would otherwise step the other one too far.
void Document::seek_to(std::size_t position) {
    store_.seek(position == 0 ? before_everything : blocks_[position - 1]);
    applied_ = position;
}

Status Document::open(std::string_view path) {
    const FileResult file = FileSystem::read(path);
    if (!file.ok) return Status{false, "open " + std::string(path) + ": " + file.text};
    // TODO: reset the document, parse the bytes into it, and only then take the path
    // as the document's own. Until the parser exists the read is only reported, and
    // the document is left as it was - which is why a read that failed leaves no
    // path behind either.
    return Status{false, "open " + std::string(path) + ": the SVG parser is not implemented yet (" +
                             std::to_string(file.text.size()) + " bytes read)"};
}

const std::string& Document::source_path() const { return source_path_; }

Status Document::save(std::optional<std::string_view> path) {
    const std::string target = path ? std::string{*path} : source_path_;
    if (target.empty()) {
        return Status{false, "this document came from nowhere yet: save <path> says where to write it"};
    }

    std::string svg;
    const Status serialized = serialize(store_, svg);
    if (!serialized.ok) return Status{false, "save " + target + ": " + serialized.message};
    const FileResult file = FileSystem::write(target, svg);
    if (!file.ok) return Status{false, "save " + target + ": " + file.text};
    return Status{true, "saved " + target};
}

void Document::reset(std::optional<LengthPercentage> width, std::optional<LengthPercentage> height) {
    store_.reset(std::move(width), std::move(height));
    next_diff_id_ = 1;
    blocks_.clear();
    applied_ = 0;
    source_path_.clear();
}

void Document::clear_history() {
    store_.clear_history();
    blocks_.clear();
    applied_ = 0;
}

Transaction::Transaction(Document& document) : document_(document), diff_id_(document.next_diff_id_++) {
    document_.store_.begin(diff_id_);
}

Transaction::~Transaction() {
    if (closed_) return;
    // The hives must never be left with a transaction open, and a destructor may
    // not throw, so a rollback that fails anyway is swallowed here.
    try {
        rollback();
    } catch (...) {
    }
}

void Transaction::require_open() const {
    if (closed_) throw std::logic_error("Transaction: this edit is already closed");
}

Id Transaction::append_child(Id parent, Node payload) { return insert_at(parent, no_id, std::move(payload)); }

void Transaction::attach_last(Id parent, Id node) { attach_at(parent, no_id, node); }

Id Transaction::insert_at(Id parent, Id before, Node payload) {
    require_open();
    const Id child = document_.store_.insert_at(parent, before, std::move(payload));
    wrote_ = true;
    return child;
}

void Transaction::attach_at(Id parent, Id before, Id node) {
    require_open();
    document_.store_.attach_at(parent, before, node);
    wrote_ = true;
}

void Transaction::update(Id node, Node payload) {
    require_open();
    document_.store_.update(node, std::move(payload));
    wrote_ = true;
}

void Transaction::erase(Id node) {
    require_open();
    document_.store_.erase_subtree(node);
    wrote_ = true;
}

void Transaction::commit() {
    if (closed_) return;
    closed_ = true;
    document_.store_.commit();
    if (!wrote_) return;  // an empty transaction leaves no block behind
    document_.blocks_.resize(document_.applied_);  // drop what a rollback deprecated
    document_.blocks_.push_back(diff_id_);
    ++document_.applied_;
}

void Transaction::rollback() {
    if (closed_) return;
    closed_ = true;
    document_.store_.rollback();
}
