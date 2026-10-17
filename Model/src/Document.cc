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

}  // namespace

Id Document::root() const { return store_.root(); }

std::optional<Node> Document::node(Id id) const {
    if (!store_.has_node(id)) return std::nullopt;
    return store_.record(id);
}

bool Document::has_node(Id id) const { return store_.has_node(id); }

std::vector<Id> Document::children(Id parent) const { return store_.children(parent); }

std::optional<Id> Document::parent(Id node) const {
    const Id above = store_.parent(node);
    if (above == no_id) return std::nullopt;
    return above;
}

std::optional<Id> Document::find(std::string_view svg_id) const {
    if (svg_id.empty()) return std::nullopt;

    // No index yet: a walk cannot go stale, and the tree is small. The arena hands
    // out no way to list its rows, so the tree is the only index there is.
    std::vector<Id> pending{store_.root()};
    while (!pending.empty()) {
        const Id current = pending.back();
        pending.pop_back();
        if (!store_.has_node(current)) continue;
        if (const Node& payload = store_.record(current);
            std::holds_alternative<SVGNode>(payload) &&
            std::get<SVGNode>(payload).identity_attr.id == svg_id) {
            return current;
        }
        for (const Id child : store_.children(current)) pending.push_back(child);
    }
    return std::nullopt;
}

Transaction Document::transaction() { return Transaction{*this}; }

bool Document::can_undo() const { return store_.can_undo(); }
bool Document::can_redo() const { return store_.can_redo(); }
void Document::undo() { store_.undo(); }
void Document::redo() { store_.redo(); }
std::size_t Document::depth() const { return store_.depth(); }
std::size_t Document::position() const { return store_.position(); }

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
    // What was written is what the file says from here on, so the way back to the
    // state before it goes with the history that remembered it.
    clear_history();
    return Status{true, "saved " + target + ", and the history starts from here"};
}

void Document::reset(std::optional<LengthPercentage> width, std::optional<LengthPercentage> height) {
    store_.reset(std::move(width), std::move(height));
    source_path_.clear();
}

void Document::clear_history() { store_.clear_history(); }

Transaction::Transaction(Document& document) : document_(document), edit_(document.store_.begin()) {}

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
    return document_.store_.insert_at(edit_, parent, before, std::move(payload));
}

void Transaction::attach_at(Id parent, Id before, Id node) {
    require_open();
    document_.store_.attach_at(edit_, parent, before, node);
}

void Transaction::update(Id node, Node payload) {
    require_open();
    document_.store_.update(edit_, node, std::move(payload));
}

void Transaction::erase(Id node) {
    require_open();
    document_.store_.erase_subtree(edit_, node);
}

void Transaction::commit() {
    if (closed_) return;
    closed_ = true;
    document_.store_.commit(edit_);
}

void Transaction::rollback() {
    if (closed_) return;
    closed_ = true;
    document_.store_.rollback(edit_);
}
