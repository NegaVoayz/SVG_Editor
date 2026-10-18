#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "History.hh"
#include "Status.hh"
#include "SVGNode.hh"
#include "TreeStore.hh"

class Transaction;

// The document is the tree, its history, and the file it came from. It has two
// faces on purpose:
//
//   - the viewing API below never moves the history: root(), node(), children()
//     and find() read the tree the way it currently stands;
//   - the editing API is transaction(), a tree-level transaction, which is what
//     the Editor uses to turn one user intent into one undoable block.
//
// A handle (an Id) is the node's virtual id in the hive. Virtual ids are never
// reused, so a handle keeps naming the same stored record across edits and
// history moves; a record that is removed comes back with the same id when the
// snapshot that still shows it is selected. It is not a name to put in a file:
// the durable name of an element is its SVG id attribute, which is what find()
// looks up.
class Document {
public:
    // No document yet: there is no root until reset() makes a fresh one or open()
    // reads one. This is the state the front end starts in, and the reason every
    // command but `init` and `read` can be refused before either has happened.
    Document() = default;

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&&) = delete;
    Document& operator=(Document&&) = delete;

    // --- viewing ---

    // The root <svg> node, or no_id while there is no document.
    Id root() const;

    // Whether a root exists. The root is the baseline, so erasing its children keeps
    // the document.
    bool has_document() const;

    // A copy of the payload stored at that handle, or nothing when no live node is
    // there. The hive exposes only immutable versions, but the document hands out
    // a copy so a caller's payload is not tied to the lifetime of a snapshot.
    std::optional<Node> node(Id id) const;

    // Whether a live document node lives at that handle, without copying its payload.
    // Resolving a `#<handle>` needs only the answer, and a payload is 416 bytes today.
    bool has_node(Id id) const;

    // The children of a node, in document order. Empty for a handle that holds
    // no node.
    std::vector<Id> children(Id parent) const;

    // The node a node sits inside, or nothing for the root, which sits inside
    // nothing. A command about order or about a level needs this: what `front` moves
    // a node to the front of, and what `ungroup` releases children into.
    std::optional<Id> parent(Id node) const;

    // The node whose SVG id attribute is this string. An empty id names nothing.
    std::optional<Id> find(std::string_view svg_id) const;

    // --- editing ---

    // One tree-level transaction: see Transaction below.
    Transaction transaction();

    // --- history ---

    bool can_undo() const;
    bool can_redo() const;
    // Move one whole transaction back or forward, and return what that did to the
    // visible tree.
    TouchedNodes undo();
    TouchedNodes redo();

    // How many transactions the history holds, and how many of them are applied.
    std::size_t depth() const;
    std::size_t position() const;

    // --- the file the document came from (Model/FileSystem) ---

    // Where the document was last read from, empty when it came from nowhere. A
    // save with no path of its own writes here.
    const std::string& source_path() const;

    // Read a document from a file, and write it to one. A read that fails leaves the
    // document as it was, so a document whose read failed did not come from that
    // file and has no path to save back to.
    Status open(std::string_view path);
    Status save(std::optional<std::string_view> path);

    // --- the context ---

    // Back to a fresh document: an empty tree, the canvas it is given (default 800x600
    // for whichever side is missing), no history, and no file behind it.
    void reset(std::optional<LengthPercentage> width, std::optional<LengthPercentage> height);

    // Forget the history: the tree stays as it is and becomes the baseline, so
    // nothing before this point can be undone or redone. A saved document and a
    // resized canvas are checkpoints of that kind - they are what the file and the
    // frame behind the drawing now say, and undoing past them would leave the two
    // disagreeing about a state nobody wrote down.
    void clear_history();

    friend class Transaction;

private:
    // The arena is a member, not something behind another pointer: a document and
    // the tree it holds are built and destroyed as one object. The hive owns its
    // storage and cannot be moved, so neither can the document that holds it.
    TreeStore store_;
    std::string source_path_;  // the file open() read, if it read one
};

// One user-level edit. The ops are applied to the tree as they come, and the
// whole group is one block of the hive history, so undo() and redo() take the
// edit back and forward as a unit.
//
// A transaction that is destroyed without commit() rolls back, which is what
// makes an edit that fails half way harmless. Only one transaction can be open at
// a time. A read through Document while one is open sees the checkout, not the
// staged edit, and a second transaction must not be opened: the hive has one
// writer, so it would wait behind the first.
class Transaction {
public:
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    // Add a new node as the last child of a parent. The last place is the cheap
    // end for the link layout, and the new node's handle is returned.
    Id append_child(Id parent, Node payload);

    // Move an existing node under a parent, last among its siblings. The node
    // keeps its own children, and a node cannot be moved into its own subtree.
    void attach_last(Id parent, Id node);

    // The same two edits into a place of their own: the node lands immediately
    // before `before` among the parent's children, and no_id is the last place
    // again. A front end asks for one of these when a command is about order rather
    // than about being last - `front`, and the two halves of `group` and `ungroup`.
    Id insert_at(Id parent, Id before, Node payload);
    void attach_at(Id parent, Id before, Id node);

    // Replace a node's payload and leave its place in the tree alone.
    void update(Id node, Node payload);

    // Drop a node and everything under it. The active checkout treats the removed nodes
    // as absent; older snapshots still hold their payloads, so undo brings the same
    // handles back instead of allocating new ones.
    void erase(Id node);

    void commit();    // keep the edit as one block of the history
    void rollback();  // drop it: nothing of it is left to redo

private:
    friend class Document;
    explicit Transaction(Document& document);
    void require_open() const;

    Document& document_;
    TreeStore::Edit edit_;
    bool closed_ = false;
};
