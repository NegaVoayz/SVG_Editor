# SVG Editor Model

The document's storage and history: the two APIs the rest of the pipeline sees, the
checkpoints that reset history, the MVCC tree, the cost of an edit, and the transform.
The module map is in [Structure.md](Structure.md); the instructions and their vocabulary
are in [Pipeline.md](Pipeline.md).

## Two APIs, one tree

Model shows two faces on purpose:

```C++
// viewing: never moves the history, and never writes
Id root() const;
std::optional<Node> node(Id id) const;
std::vector<Id> children(Id parent) const;
std::optional<Id> parent(Id node) const;
std::optional<Id> find(std::string_view svg_id) const;
std::size_t element_count() const;

// editing: one user intent, one undoable block
Transaction transaction();
Id append_child(Id parent, Node payload);
void attach_last(Id parent, Id node);
Id insert_at(Id parent, Id before, Node payload);   // before a sibling; no_id is the end
void attach_at(Id parent, Id before, Id node);
void update(Id node, Node payload);
void erase(Id node);
void commit();  void rollback();

// history: the Model owns it, so the Planner never sees a commit id
TouchedNodes undo();  TouchedNodes redo();
bool can_undo() const;  bool can_redo() const;
bool undo_barred_by_io() const;

// the context: which document this is, rather than what is in it
const std::string& source_path() const;
Status open(std::string_view path);                  // staged parse, then replace the document
Status save(std::optional<std::string_view> path);   // no path: where it was read from
void reset(std::optional<LengthPercentage> width, std::optional<LengthPercentage> height);
void clear_history();                                // the tree stays and becomes the baseline
```

`element_count()` counts the SVG elements below the root. Character data is not an
element, and the root `<svg>` is the document rather than one of its own elements; the
same count is what `read` and `save` report.

A `Transaction` is an RAII handle. The ops are applied to the tree as they come,
and the destruction without `commit()` rolls the whole thing back, so an edit that
fails half way - a bad handle, a parser that refuses, an exception - leaves
nothing behind. That is also why the Editor is the only writer: undo and redo are
edits of the history, not of the tree, and they go through the same path.

## The canvas, and the checkpoints

The canvas is not a field of the document: it is the root `<svg>` element's own `width`
and `height`, which is where SVG writes it and what the serializer writes back out
of. `canvas <w> [h]` is therefore an ordinary edit of one payload - the root's - and
`tree` reads the size off the root row like anything else about an element, which is why
`canvas` with no operand is exactly the read `show /` is. The default is 800x600 in user
units, which is the bare-number form SVG writes and what the front end shows before
anything is set.

Three instructions drop the history: `canvas`, `save`, and `init`, which replaces the
document instead of editing it. What the first two have in common is that the tree they
leave behind is the state the file, or the frame behind the drawing, says it is - so the
way back to the state before them is a state nobody wrote down. `clear_history()` is that
drop: the tree stays as it is and becomes the baseline. `init` goes
further: it creates the root when none exists, and otherwise replaces the whole
document. The canvas comes with the new root, and there is nothing of the old document
left to undo into, so it lands on the Model's `reset()` rather than in a transaction.

## The tree in a hive

The tree is one `mvcc_hive::MVCCHive<Node>`. There is no side table for parent or
child links: a `Node` is the whole record, and its place in the tree is part of it.
Transform chains are the one side table, because a list of transform steps is not
part of the tree's shape and is shared by every historical version of a node.

```C++
struct SVGNode {
    IdentityAttr identity_attr;
    PaintAttr paint_attr;
    TransformAttr transform_attr;
    Id parent_id = no_id;
    Id previous_sibling = no_id;
    Id next_sibling = no_id;
    std::variant<Shape, Container, Text> data;
};

struct PlainText {
    std::string content;
    Id parent_id = no_id;
    Id previous_sibling = no_id;
    Id next_sibling = no_id;
};

using Node = std::variant<SVGNode, PlainText>;

struct ChildAttr { Id first_child = no_id; Id last_child = no_id; };

struct TransformLink {
    Transform transform;
    Id previous = no_id;
};

struct TransformAttr { Id head = no_id; };
```

A container or a text carries `ChildAttr`, a shape does not. The children of a
node are a doubly linked list: start at `first_child`, follow `next_sibling`, and
stop at `no_id`. A parent keeps both ends, so appending is O(1); a node keeps both
of its sibling links, so placing it in front of another child or removing it is
O(1) too. There is no backwards edge table to consult.

Forgetting a node is a hive erase, not an SVG payload. An older snapshot still
holds the immutable versions that showed the node, so undo brings the same
virtual id back without allocating a new one. The live model never contains a
fabricated `<removed />` element.

Which types can do what remains a compile-time fact, checked without listing the
kinds. `Linked<T>` requires the three link members themselves, and
`all_linked()` folds over the alternatives of `Node`, so an alternative that
forgets one is a build error. `ChildHolder<T>` requires both ends of the child
list; a type that only claims the capability, or stores a different type there,
does not satisfy it. The store's one runtime question - what is this payload? -
is still a `std::visit`, and the alternatives answer with `if constexpr`.

One user intent is one MVCCHive transaction, and therefore one commit. A move
rewrites the moved node's links and the links of its old and new neighbours; a
payload edit writes a new version of one node. The hive records a normalized
diff per changed virtual id, not a whole copy of a block per hive, so there is no
second, third or fourth history to keep in step and no fourfold amplification
when one user instruction touches several rows. This is also why the stored type
may be as large as `Node`: the hive only constructs a new version of the records
the edit actually changes.

The transaction API is one operation per shape of tree edit:

- `append_child` writes the node, the old last child and the parent's end links.
- `attach_last` detaches the node from its old neighbours and links it at the
  new end. The node's payload is never moved into another record.
- `insert_at` / `attach_at` do the same splice before a sibling. Because
  the list is doubly linked, the anchor's place is known without walking the
  level, and `no_id` still means the end.
- `update` replaces the payload and copies the existing place and child-list
  ends into the replacement, so a payload cannot bring another node's links
  with it.
- `erase_subtree` detaches the root of the subtree and erases every record in
  it.

The tree refuses what SVG does not allow: only a payload with `ChildAttr` takes
children, the root cannot be moved or erased, a node cannot be moved inside its
own subtree, and an update cannot change whether a node holds children.

## What one edit costs

A `set` is the smallest write: one changed virtual id, one new `Node` version, one
row-version node and one commit diff. Nothing scales with the number of fields in the
record or with the old diff-hive's parallel tables. The record itself is the unit of
copying, though: `Node` is 416 bytes with the current MinGW/libstdc++ layout, mostly
`Text`'s optional values and the strings, so a one-field edit still constructs a
416-byte version. If that becomes the hot path, the lever is the record layout rather
than another hive.

Current-view reads resolve a virtual id to one target, O(1), and then pay for the payload
copy at the API boundary. The Planner's `tree` view is a full walk plus one `Node` copy per
row, and `find()` is still a walk, so a command naming an SVG id costs O(N) before any
write. The handle form (`#<id>`) avoids that walk; an id index is the next read-side
improvement. Resolving a handle uses `Document::has_node()`, so it does not copy the
record merely to learn that the record exists; only a read that returns the payload pays
for the copy.

History is the other axis. MVCCHive 2.1.0 keeps commit records and diffs in fixed-size
blocked vectors, so appending a commit no longer copies the retained commit log. The
remaining history cost is block reclamation: `VersionStore::drain_retirements()` calls `release_empty_blocks()`,
which scans every owned version block on every commit. With a 416-byte `Node` that is one
block per 32 versions, so per-edit latency still rises with retained history even though
each edit writes one version. Tracking only the blocks a retirement actually empties, or
putting a cap on retained history, is the next step before very long sessions.

On this machine (MinGW GCC 14.2, Release), a `set` at 50k history entries costs about
5.1 us (195k sets/s), reading one node about 0.49 us, and `show #handle` about 0.95 us.
The same run measures about 3.8, 13 and 31 us per set at 20k, 100k and 200k history,
which is that block scan. Bulk loading has a separate cost: `Transaction::emplace()`
reserves its created-target and reclamation vectors for exactly one more element per
insertion, so building 16k nodes in one transaction takes about 0.7 s rather than
scaling linearly. The loader commits a large file in chunks and clears the history
when the load is finished.

The public history `Diff` in 2.1.0 is a target-free record of
`commit_id`, `virtual_id`, and a `DiffKind` of `insert`, `update`, or
`remove`. `TreeStore` uses `virtual_id` for the touched node and `kind` for
the `TouchedKind`; no storage target or payload copy is needed to classify it.

Internally the hive's `DiffRecord` is 32 bytes and 32-byte aligned, and its
`CommitRecord` is 32 bytes too. `BlockedVector` stores both in fixed-capacity
blocks (`std::array<T, 2048>`) rather than one growing contiguous vector, so
appending a commit does not move the retained records and the history scan walks
dense, aligned storage. That is the performance side of the history choice: a
whole-state memento per edit would duplicate payloads, while an inverse per
command would duplicate undo logic; the hive writes one small record per changed
virtual id and keeps the log compact enough to scan cheaply.

Handles are MVCCHive virtual ids. They are monotonically allocated and never
reused, so a handle cannot silently name a different node later. The durable
name of an element is still its SVG id attribute, which is what `find()` walks
the tree for. Model keeps no separate id index, so the tree is the only index
there is - anything not reachable from the root is unreachable.

`no_id` is `std::numeric_limits<Id>::max()`, and `Id` has the same 64-bit width
as a hive virtual id. Nothing is reserved in front of the first record: the root
is virtual id 0, and `no_id` is what every link that leads nowhere points at.
The document itself starts without a root: `root()` is `no_id`, `has_document()`
is false, and the Planner refuses every instruction except `init` and `read` with
`error: no document. use 'init' or 'read' first`. `init` is what creates the root,
and from then on the root is the baseline, so "undo everything" lands on an empty
document rather than no document.

## History, checkout and touched changes

MVCCHive owns a recoverable checkout. `undo()` moves the checkout to the parent
commit and rebuilds the current-target view; `redo()` moves it forward while the
redo tail is intact. `TreeStore` keeps only the current checkout snapshot and
the depth/position counters. It deliberately does not keep snapshots for later
redo states: a future snapshot pins that commit, and a new edit after undo must
be free to truncate the abandoned tail.

`commit()` from an older checkout writes a new commit on top of it and discards
the redo tail. An empty transaction makes no commit and leaves the tail intact.
Undo does not rewrite the tree: the checkout already holds the immutable
versions that were visible at that point, and an edit after undo starts directly
from that checkout.

`clear_history()` releases the current snapshot, asks the hive to truncate and
prune the history around the checkout, and makes that checkout the baseline.
`reset()` is stronger: it drops the whole hive generation and builds a new root.
A document that holds that hive by value cannot be moved, which is why
`Document` is non-copyable and non-movable.

The number of user edits is `depth_`; `position_` is where the checkout stands.
Undo and redo return `TouchedNodes`: one entry per changed virtual id with a
`TouchedKind` of `Insert`, `Update`, or `Remove`. The ids come from the hive's
public `diffs(commit_id)`, and the kind comes directly from `DiffKind`,
inverted for undo (`insert` becomes `remove`, `remove` becomes `insert`, and
`update` stays `update`). The list includes neighbours whose links changed, not
only the node an instruction named, and an erased subtree appears as every
removed id. The cost is proportional to that touched set: the hive copies its
public 24-byte `Diff` entries, then the document keeps an id and a kind for each one.
The checkout switch itself stays O(1); the touched list is the price of telling
a front end what to refresh.

## The transform

A node may carry a transform list, and each step is kept in the form SVG writes it:

```C++
struct Translate { LengthPercentage x, y; };
struct Scale { float x, y; };
struct Rotate { float degrees; };
struct Matrix { float a, b, c, d, e, f; };
using Transform = std::variant<Translate, Scale, Rotate, Matrix>;
```

The list is a persistent linked list of immutable records:

```C++
struct TransformLink {
    Transform transform;
    Id previous = no_id;
};
```

`TransformAttr` in a node is only the head index. Reading from the head towards
`no_id` gives the functions in SVG order, so `scale(2) rotate(30) scale(3) rotate(45)`
keeps all four steps and their order. Nothing composes them into a matrix, which
means one step never has to be approximated to carry another. A translate in `mm`
can sit next to a scale and stay `mm`; the unit-system rules still apply when a
command adds two lengths, but no longer restrict what a transform may contain.

The link records live in `TreeStore`, beside the nodes, and only `Model` reads or
appends them. `move ... transform` and `scale ... transform` prepend one link: the
new step is in front of the old head, which is `translate(...) <old list>` in SVG and
puts the move in the parent's coordinate system. `set transform` replaces the head
with a newly built list; an empty list clears the attribute. Because the old links
are never rewritten, undo and redo only switch a node record to the head it used to
name. A rolled-back transaction truncates the links it appended.

The cost is one extra index operation per step and an append for each edit, instead
of recomputing and rewriting the whole transform. The records are small and
contiguous, and only the edited node record is written by the hive. History can
leave unreachable links behind until a `read`/`init` clears the document; a future
compaction pass can rebuild the reachable chains without changing the public
`Document` API.

A shape moves its own coordinates instead, since it has them: `move r 1 2` changes
`x` and `y` and leaves the transform alone. That keeps a shape's coordinates in the
units they were written with, at the price of the unit rule above - a rect whose `x`
is `50%` cannot be moved by a length, and a transform could.

`scale` lands the same way round: a shape's own values are multiplied where they stand,
each in the unit it was written in, so a scale never asks the unit table anything, and a
container takes the scale as a new first transform step. `scale(factor) <old list>` leaves
the node where a move put it and grows what is drawn under it from the node's own origin;
it is the same drawing that multiplying every coordinate under the node would leave, in
one payload instead of all of them. A factor of nothing, or a negative one, is refused:
SVG can mirror and collapse, and nothing here writes either yet.

## SVG file I/O

`Model/FileSystem` still moves bytes and nothing else. Above it, Model reads XML through
tinyxml2 `11.0.0`, but every tinyxml2 type and raw pointer lives in
`TinyXml2Adapter.cc`. The adapter copies the DOM into `XmlSnapshot`, a vector arena with
index-based children and value attributes. `SvgReader` maps that snapshot to a
`ParsedSvg`, and only after the whole file passes does `Document::open()` replace the
tree. Parse failure therefore leaves the old tree, history and source path untouched.

The accepted element set is `svg`, `g`, `defs`, `symbol`, `switch`, `rect`, `circle`,
`ellipse`, `line`, `text`, `tspan` and `textPath`. Common attributes are `id`, `class`,
`fill`, `stroke`, `stroke-width` and `transform`; the geometry, text, canvas size,
`startOffset` and `href` attributes follow the element. The root stores its default
`xmlns` and an optional `viewBox`; text elements store `text-anchor` and `font-size`.
Prefixed namespace declarations are ignored. Everything else is refused: there is no
silent drop that a later `save` could turn into data loss. Colours cover hex, `rgb()`,
`none`, `currentColor`, all 148 CSS Color 4 named colours and the system-colour set;
alpha, `transparent`, `style` and element kinds such as `path` remain outside the model.
An empty or whitespace-only `transform` attribute is accepted as an empty list, just
like `transform="none"`.

Loading assigns per-kind ids such as `rect1` and `circle1` to unnamed non-root elements,
in document order. The root may stay unnamed. A non-root element whose id is `svg` is
renamed the same way, because `svg` names the root and would leave that element
unreachable. Explicit ids are reserved first, so a generated id cannot collide with an
explicit id later in the file, including the `rect1` an explicit element already holds.
Each assignment is an `AssignedId` in `model_status::Opened`; its empty `original_id`
means the element was unnamed, and a value there is the reserved id that was replaced.
The Planner turns these into `event::Opened`; the front end prints
`warning: assigned id rect1 to unnamed <rect>` or
`warning: id "svg" is reserved for the root, renamed <rect> to rect2`.

The loader commits at most 1024 nodes per transaction and then clears history, making
the loaded tree the baseline. `SvgWriter` walks the current checkout in document order
and writes deterministic UTF-8 SVG: declaration, the root namespace, fixed attribute
order, escaped attributes and text, structural indentation, and inline mixed text. A
save clears history only after the file write succeeds.

## What is not there yet

- the element kinds the Editor can create are the four shapes, a text and a group. The
  model has no `path` at all; a `defs`, `symbol` or `switch` can arrive from a file.
- the strict SVG subset has no path data, gradients or other paint-server definitions,
  style sheets, alpha channels, masks or filters. A file using them is refused
  until the corresponding model value and exhaustive visitors exist.
- a `find()` index: the view walks the tree on every call, which cannot go stale
  and is cheap enough while the tree is small.
- retention: the hive offers `prune_before` and `clear_history`, and `TreeStore` uses
  the latter at checkpoints. It holds only the current checkout snapshot, not one per
  history step. A retention policy that drops old commits and then prunes the hive is
  still not exposed.
- escaping in the front end: a quoted run is one word and there is no way to put a quote
  inside one, so a text cannot spell `"` yet.

