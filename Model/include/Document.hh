#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
// A handle (an Id) names a slot in the node arena and keeps naming the same node
// while that node lives, across any number of edits and history moves. It is not
// a name to put in a file: the durable name of an element is its SVG id
// attribute, which is what find() looks up.
class Document {
public:
    // An empty document, with its root <svg> element already in place.
    Document() = default;

    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // --- viewing ---

    // The root <svg> node. An empty document still has one.
    Id root() const;

    // A copy of the payload stored at that handle, or nothing when no node lives
    // there. It is a copy on purpose: an edit moves payloads between slots, so
    // anything reaching into the arena could be pointing at another node by the
    // time it is read.
    std::optional<Node> node(Id id) const;

    // The children of a node, in document order. Empty for a handle that holds
    // no node.
    std::vector<Id> children(Id parent) const;

    // The node whose SVG id attribute is this string. An empty id names nothing.
    std::optional<Id> find(std::string_view svg_id) const;

    // --- editing ---

    // One tree-level transaction: see Transaction below.
    Transaction transaction();

    // --- history ---

    bool can_undo() const;
    bool can_redo() const;
    void undo();  // one whole transaction back
    void redo();  // and forward again

    // How many transactions the history holds, and how many of them are applied.
    std::size_t depth() const;
    std::size_t position() const;

    // --- the file the document came from (Model/FileSystem) ---

    Status open(std::string_view path);
    Status save(std::string_view path) const;

    friend class Transaction;

private:
    void seek_to(std::size_t position);

    // The arena is a member, not something behind another pointer: a document and
    // the tree it holds are built, moved and destroyed as one object.
    TreeStore store_;
    Id next_diff_id_ = 1;      // hive diff ids start at 1 and never decrease
    std::vector<Id> blocks_;   // the diff id of every committed edit, oldest first
    std::size_t applied_ = 0;  // how many of them the two hives are showing
};

// One user-level edit. The ops are applied to the tree as they come, and the
// whole group is one block of the hive history, so undo() and redo() take the
// edit back and forward as a unit.
//
// A transaction that is destroyed without commit() rolls back, which is what
// makes an edit that fails half way harmless. Only one transaction can be open at
// a time: reading the document while one is open is fine, editing it through a
// second one is not.
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

    // Replace a node's payload and leave its place in the tree alone.
    void update(Id node, Node payload);

    // Drop a node and everything under it. The handles of the removed nodes stop
    // working and their slots become reusable; undo brings all of them back.
    void erase(Id node);

    void commit();    // keep the edit as one block of the history
    void rollback();  // drop it: nothing of it is left to redo

private:
    friend class Document;
    explicit Transaction(Document& document);
    void require_open() const;

    Document& document_;
    Id diff_id_;
    bool wrote_ = false;
    bool closed_ = false;
};
