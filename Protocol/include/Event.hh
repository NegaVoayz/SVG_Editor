#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "History.hh"
#include "NodeRef.hh"
#include "SVGNode.hh"

// What a successful instruction did, as data. An event names handles and values rather
// than a sentence: the text front end prints it, a graphical front end can use the same
// event to refresh the part of its display the edit touched, and neither has to read a
// message written by a lower layer.
//
// The event is deliberately a report of the edit, not a replayable patch. The document is
// still the source of truth; an event says which part of it moved.
namespace event {

struct Created {
    Id node;
    std::string element;
    std::string svg_id;
    Id parent;
    std::string parent_element;
    std::string parent_svg_id;
    bool parent_named;
};

struct Deleted {
    Id node;
    NodeRef named;
};

struct Moved {
    Id node;
    LengthPercentage dx;
    LengthPercentage dy;
};

struct Scaled {
    Id node;
    float factor;
};

struct AddedTo {
    Id node;
    Id parent;
};

struct MovedToFront {
    Id node;
    Id parent;
};

struct MovedToBack {
    Id node;
    Id parent;
};

struct Grouped {
    Id node;
    std::string svg_id;
    Id parent;
    std::size_t members;
};

struct Ungrouped {
    Id node;
    NodeRef named;
    Id parent;
    std::size_t members;
};

struct AttributeSet {
    Id node;
    std::string attribute;
};

struct ContentSet {
    Id node;
};

struct CanvasSet {
    std::optional<LengthPercentage> width;
    std::optional<LengthPercentage> height;
};

struct Undone {
    std::size_t position;
    std::size_t depth;
    TouchedNodes touched;
};

struct Redone {
    std::size_t position;
    std::size_t depth;
    TouchedNodes touched;
};

struct NothingMoved {};

struct AlreadyFront {
    NodeRef node;
};

struct AlreadyLast {
    NodeRef node;
};

struct Initialized {
    std::optional<LengthPercentage> width;
    std::optional<LengthPercentage> height;
};

struct Opened {
    std::string path;
};

struct Saved {
    std::string path;
};

}  // namespace event

using Event = std::variant<event::Created, event::Deleted, event::Moved, event::Scaled, event::AddedTo,
                           event::MovedToFront, event::MovedToBack, event::Grouped, event::Ungrouped,
                           event::AttributeSet, event::ContentSet, event::CanvasSet, event::Undone, event::Redone,
                           event::NothingMoved, event::AlreadyFront, event::AlreadyLast, event::Initialized,
                           event::Opened, event::Saved>;
