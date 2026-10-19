#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "EditSelectors.hh"
#include "History.hh"
#include "SVGNode.hh"
#include "SvgDiagnostic.hh"

// What a successful instruction did, as data. An event names the elements and values the
// edit changed rather than a sentence: the text front end prints it, a graphical front end
// can use the same event to refresh the part of its display the edit touched, and neither
// has to read a message written by a lower layer.
//
// The event is deliberately a report of the edit, not a replayable patch. The document is
// still the source of truth; an event says which part of it moved.
namespace event {

// The name a node is shown by: its element and the SVG id its document gives it. The
// hive's virtual id is deliberately absent; it is an implementation detail of storage,
// not a name a front end can print or a user can type.
struct NodeName {
    std::string element;
    std::string svg_id;
};

struct Created {
    NodeName node;
    NodeName parent;
    bool parent_named;
};

struct Deleted {
    NodeName node;
    std::size_t descendants;
};

struct Moved {
    NodeName node;
    LengthPercentage dx;
    LengthPercentage dy;
    EditMode mode = EditMode::Params;
    std::size_t nodes = 1;
};

struct Scaled {
    NodeName node;
    float factor;
    EditMode mode = EditMode::Params;
    std::size_t nodes = 1;
};

struct AddedTo {
    NodeName node;
    NodeName parent;
};

struct MovedToFront {
    NodeName node;
    NodeName parent;
};

struct MovedToBack {
    NodeName node;
    NodeName parent;
};

struct Grouped {
    NodeName node;
    NodeName parent;
    std::size_t members;
};

struct Ungrouped {
    NodeName erased;
    NodeName parent;
    std::size_t members;
};

struct AttributeSet {
    NodeName node;
    std::string attribute;
};

struct ContentSet {
    NodeName node;
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
    NodeName node;
};

struct AlreadyBack {
    NodeName node;
};

struct Initialized {
    std::optional<LengthPercentage> width;
    std::optional<LengthPercentage> height;
};

struct Opened {
    std::string path;
    std::vector<AssignedId> assigned_ids;
    std::size_t elements = 0;
};

struct Saved {
    std::string path;
    std::size_t elements = 0;
};

}  // namespace event

using Event = std::variant<event::Created, event::Deleted, event::Moved, event::Scaled, event::AddedTo,
                           event::MovedToFront, event::MovedToBack, event::Grouped, event::Ungrouped,
                           event::AttributeSet, event::ContentSet, event::CanvasSet, event::Undone, event::Redone,
                           event::NothingMoved, event::AlreadyFront, event::AlreadyBack, event::Initialized,
                           event::Opened, event::Saved>;
