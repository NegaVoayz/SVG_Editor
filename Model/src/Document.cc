#include "Document.hh"

#include <stdexcept>
#include <utility>
#include <variant>

#include "FileSystem.hh"
#include "LoadDocument.hh"
#include "SvgReader.hh"
#include "SvgWriter.hh"
#include "TinyXml2Adapter.hh"
#include "TreeStore.hh"

Id Document::root() const { return store_.root(); }

bool Document::has_document() const { return store_.root() != no_id; }

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
    if (!has_document()) return std::nullopt;

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
TouchedNodes Document::undo() { return store_.undo(); }
TouchedNodes Document::redo() { return store_.redo(); }
std::size_t Document::depth() const { return store_.depth(); }
std::size_t Document::position() const { return store_.position(); }

Status Document::open(std::string_view path) {
    const FileResult file = FileSystem::read(path);
    if (!std::holds_alternative<FileBytes>(file)) return model_status::CannotReadFile{std::string{path}};
    const FileBytes& bytes = std::get<FileBytes>(file);

    const XmlReadResult xml = read_xml(bytes.bytes);
    if (const XmlReadError* error = std::get_if<XmlReadError>(&xml)) {
        const SvgProblem problem =
            error->kind == XmlReadError::Kind::NoRoot ? SvgProblem::NoRootElement : SvgProblem::MalformedXml;
        return model_status::InvalidSvg{SvgDiagnostic{problem, error->line, {}, {}, {}, error->detail}};
    }

    const SvgReadResult parsed = read_svg(std::get<XmlSnapshot>(xml));
    if (const SvgDiagnostic* diagnostic = std::get_if<SvgDiagnostic>(&parsed)) {
        return model_status::InvalidSvg{*diagnostic};
    }

    const ParsedSvg& document = std::get<ParsedSvg>(parsed);
    load_parsed(store_, document);
    source_path_ = std::string{path};
    return model_status::Opened{document.assigned_ids};
}

const std::string& Document::source_path() const { return source_path_; }

Status Document::save(std::optional<std::string_view> path) {
    if (!has_document()) {
        return model_status::NoDocument{};
    }

    const std::string target = path ? std::string{*path} : source_path_;
    if (target.empty()) {
        return model_status::MissingSourcePath{};
    }

    const std::string svg = write_svg(store_);
    const FileResult file = FileSystem::write(target, svg);
    if (!std::holds_alternative<FileWritten>(file)) return model_status::CannotWriteFile{target};
    // What was written is what the file says from here on, so the way back to the
    // state before it goes with the history that remembered it.
    clear_history();
    return model_status::Ok{};
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
