#pragma once

#include <optional>
#include <string>
#include <variant>

#include "Document.hh"

// How an instruction names a node, and what that name turns out to be. Both directions of
// the pipeline ask this question - the Editor writes to the node a reference names and the
// Planner reads it - so the answer is written once, next to the vocabulary that defines
// what a reference is. What to do about "nothing" is not written here: the Editor refuses
// the edit, and the Planner reports the miss.

// The three ways to name one: the root, a handle, or an SVG id.
struct RootRef {};
struct HandleRef { Id id; };
struct SvgIdRef { std::string name; };
using NodeRef = std::variant<RootRef, HandleRef, SvgIdRef>;

// The node a reference names, or nothing when nothing is called that.
std::optional<Id> look_up(const Document& document, const NodeRef& reference);

inline std::optional<Id> look_up(const Document& document, const NodeRef& reference) {
    if (std::holds_alternative<RootRef>(reference)) {
        const Id root = document.root();
        if (root == no_id) return std::nullopt;
        return root;
    }
    if (std::holds_alternative<HandleRef>(reference)) {
        const Id id = std::get<HandleRef>(reference).id;
        if (!document.has_node(id)) return std::nullopt;
        return id;
    }
    const std::string& name = std::get<SvgIdRef>(reference).name;
    // The root element is an <svg>, so its element name is a reserved alias for the root.
    // No other node may keep this id: the Editor refuses it and the reader renames it, so
    // the alias cannot shadow an element that the user cannot reach.
    if (name == "svg") {
        const Id root = document.root();
        if (root != no_id) return root;
    }
    return document.find(name);
}
