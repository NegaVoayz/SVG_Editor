#pragma once

#include <cstddef>
#include <memory>
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
// still the source of truth; an event says which part of it moved. History keeps the edit
// event under the document-wide commit id so undo and redo can report the original
// structured operation and every hive can resolve the same checkout.
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
    std::string content;
};

struct CanvasSet {
    std::optional<LengthPercentage> width;
    std::optional<LengthPercentage> height;
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

// Every event a history entry can carry. Undo and redo are reports about a history move,
// not entries of their own, so they are kept out of the replayable edit vocabulary.
using EditEvent = std::variant<Created, Deleted, Moved, Scaled, AddedTo, MovedToFront, MovedToBack, Grouped,
                               Ungrouped, AttributeSet, ContentSet, CanvasSet, NothingMoved, AlreadyFront,
                               AlreadyBack, Initialized, Opened, Saved>;

// One committed edit in the replay log. The id is the document-wide id from Model,
// not the hive's local commit id, so a branch or a second hive cannot make an outcome
// point at the wrong state. The log is indexed by this id.
struct HistoryEntry {
    GlobalCommitId commit = 0;
    std::shared_ptr<const EditEvent> outcome;
};

struct Undone {
    std::size_t position;
    std::size_t depth;
    TouchedNodes touched;
    std::shared_ptr<const EditEvent> original;
    // The global commit the checkout landed on. A front end with more than one hive
    // resolves the same value through each hive to undo them together.
    GlobalCommitId checkout = 0;
};

struct Redone {
    std::size_t position;
    std::size_t depth;
    TouchedNodes touched;
    std::shared_ptr<const EditEvent> original;
    GlobalCommitId checkout = 0;
};

}  // namespace event

using Event = std::variant<event::Created, event::Deleted, event::Moved, event::Scaled, event::AddedTo,
                           event::MovedToFront, event::MovedToBack, event::Grouped, event::Ungrouped,
                           event::AttributeSet, event::ContentSet, event::CanvasSet, event::Undone, event::Redone,
                           event::NothingMoved, event::AlreadyFront, event::AlreadyBack, event::Initialized,
                           event::Opened, event::Saved>;
