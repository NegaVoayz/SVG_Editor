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
void commit(CommitId = 0);  void rollback();

// history: the Model owns it, so the Planner never sees a commit id
TouchedNodes undo();  TouchedNodes redo();
bool can_undo() const;  bool can_redo() const;
bool undo_barred_by_io() const;
std::optional<CommitId> commit_at(std::size_t position) const;  // one edit's checkout

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
The transform is part of the record too, as one affine matrix, so a node version and
its geometry always agree without a second structure to keep in step.

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

struct TransformAttr { Transform transform{}; };
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

History is the other axis. MVCCHive 2.2.0 keeps commit records and diffs in fixed-size
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

The public history `Diff` in 2.2.0 is a target-free record of
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
the redo tail. An argument-free empty transaction makes no commit and leaves the
tail intact. An explicit `commit(id)` with no changes still publishes an empty
commit and advances the checkout, which is what lets a second hive stay aligned.
Undo does not rewrite the tree: the checkout already holds the immutable
versions that were visible at that point, and an edit after undo starts directly
from that checkout.

`clear_history()` releases the current snapshot, asks the hive to truncate and
prune the history around the checkout, and makes that checkout the baseline.
`reset()` is stronger: it drops the whole hive generation and builds a new root.
A document that holds that hive by value cannot be moved, which is why
`Document` is non-copyable and non-movable.

The number of user edits is `depth()`; `position()` is where the checkout stands.
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

MVCCHive can publish a caller-supplied commit id, and the editor uses one
process-wide sequence to hand that id to every hive taking part in an edit. The
native commit id is therefore the shared checkout id; there is no second mapping
to keep in step. A branch after undo gets a fresh id (explicit ids are never
reused), and a checkpoint keeps the id of the checkout it preserves. The editor
allocates after the hives have their baselines, because `reset`, `read` and the
loader also take ids from the same sequence. An automatic `commit()` still exists
for internal no-op checks, and the coordinator observes any automatic id so a
later explicit commit cannot collide with it.

## The transform

A node carries one affine transform matrix:

```C++
struct Transform {
    float a = 1.0F, b = 0.0F, c = 0.0F, d = 1.0F;
    LengthPercentage e, f;  // translation
};
```

It maps a point as

```C++
x' = a*x + c*y + e
y' = b*x + d*y + f
```

The four linear entries are plain numbers. The translation is a vector of lengths, so a
transform can keep `1mm` in a document whose user units mean something else. Composition
uses the same unit table as a coordinate edit: a unit sum that has no exact value makes
the edit fail instead of approximating. The identity matrix means the node carries no
transform.

A transform list is composed into this matrix when a file or a command is read. The order
is SVG's order: `translate(10 0) rotate(90)` becomes `T * R`, so points are rotated first
and then translated. `move` and `scale` write a parent-space operation by pre-multiplying:
`T(dx,dy) * M` and `S(factor) * M`. A move therefore stays a move to the right even when
the node is already rotated.

The user and the serializer read the matrix in a TRS+skew form:

```text
translate(tx ty) rotate(degrees) scale(sx sy) skewX(degrees) | skewY(degrees)
```

Both skew axes are calculated. Either decomposition is exact, so the writer emits the one
whose absolute skew angle is smaller; a skewX input can therefore read back as a
rotate/scale/skewY form. Identity components are omitted. The decomposition is exact for
every finite matrix; a mirror appears as a negative scale, and a singular matrix as a zero
scale. `matrix(...)` is still accepted as input, but the output is the decomposition so a
reader can see what the transform does rather than multiplying six numbers in their head.

A shape without a transform moves its own coordinates instead, since it has them:
`move r 1 2` changes `x` and `y` and leaves the transform alone. That keeps a shape's
coordinates in the units they were written with, at the price of the unit rule above - a
rect whose `x` is `50%` cannot be moved by a length, and a transform could. A shape that
does carry a transform takes the move in its transform instead, because its `x` and `y`
are read in the local coordinate system that transform establishes, not in the parent's.
The same rule applies to a container: an untransformed container moves its descendants'
params, while a transformed one pre-multiplies the operation into its own matrix.

`scale` lands the same way round: a shape's own values are multiplied where they stand,
each in the unit it was written in, so a scale never asks the unit table anything. Its
`stroke-width` is a drawn dimension and is multiplied with the geometry. A transform-based
scale is `S(factor) * M`, which is also where a scale on a node that already carries a
transform goes; it leaves the node where a move put it and grows what is drawn under it
from the node's own origin. A factor of nothing, or a negative one, is refused: SVG can
mirror and collapse, and nothing here writes either yet.

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
An empty or whitespace-only `transform` attribute is accepted as the identity, just
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

