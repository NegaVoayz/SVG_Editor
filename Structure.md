# SVG Editor Design

## Modules

| module              | what it is                                                        | depends on                    |
| ------------------- | ----------------------------------------------------------------- | ----------------------------- |
| `Model/`            | the SVG tree: where it is stored, how it is read, how it is edited  | SwapDiffHive                  |
| `Model/FileSystem/` | bytes in and out of the document's file                             | -                             |
| `Protocol/`         | the vocabulary the pipeline passes around: the intents, `Outcome`    | Model (for the values)        |
| `Editor/`           | translates one change intent into tree transactions                 | Model, Protocol               |
| `Planner/`          | decides what one instruction means                                  | Model, Editor, Protocol       |
| `Ui/`               | the text front end: parses one line into an intent, prints the outcome; owns the document | Planner, Model, Protocol |

Three rules hold the shape together:

- **SwapDiffHive is Model's dependency.** The hive is the arena the tree is stored
  in: only `Model/` fetches it (`Model/cmake/SwapDiffHive.cmake`) and only
  `Model/` uses it - its transactions, its diff ids and its slots are Model's
  business, and no other module opens one.
- **Everything above Model reaches the document through Model.** The Editor
  writes to it, the Planner reads it, and the Planner never talks to the Editor
  about anything but a change intent.
- **Text is the ui layer's business, in both directions of the boundary.** What a
  user typed becomes a typed intent before it leaves `Ui/`, so no layer above ever
  parses a string, and no layer below knows how an instruction was written.

Because `Document` holds the arena by value, the hive's headers do reach the other
modules' include path, so the link line no longer says who may use it. The test
named `layering` reads the sources of every module but Model and fails if one of
them names SwapDiffHive.

The text front end is also the composition root: it owns the `Document` and hands
it to the Editor and the Planner. A gui front end would sit in exactly the same
place, which is why the document is owned above the Planner rather than inside it.

## What happens after one instruction

> Here [M a b...] means this process mainly requires module b in a;

1. A line is read and parsed into a typed intent: a `CreateRect`, a `Delete`, an
   `Open`, one of the alternatives of `Intent`. The values in it are model values
   by then - a `LengthPercentage`, a `Color`, a `NodeRef` - so nothing above this
   point sees a string a user typed. [M Ui] [M Protocol]
2. The Planner decides what that intent means, and that is the whole decision:
   [M Planner]
   - a **view** is served by reading the tree directly [M Model]
   - a **change** goes to the Editor, which translates it into a tree-level
     transaction [M Editor] [M Model]
   - a **context change** goes to the file layer, because it changes which
     document is being edited rather than the tree in it [M Model/FileSystem]
3. The outcome (whether it worked, and a message) travels back the way it came.
   [M Protocol] [M Ui]

### How an intent is dispatched

An instruction is a `std::variant` of one struct per thing a user can ask for, and
every layer that has to answer one does it with `std::visit` over that variant plus
one function specialization per alternative:

```C++
// the primary template is the missing case, and it is a build error
template<typename T>
Outcome handle(const T& intent) {
    static_assert(always_false<T>, "the Planner has no route for this intent");
}

template<> Outcome handle<ShowTree>(const ShowTree&) { return show_tree(); }
template<> Outcome handle<CreateRect>(const CreateRect& intent) {
    return editor_.translate(ChangeIntent{intent});
}
// ... one per instruction

Outcome dispatch(const Intent& intent) {
    return std::visit([this](const auto& value) { return handle(value); }, intent);
}
```

The point is what happens when an instruction is added: the variant grows, the
visitor has to answer the new alternative, and the primary template is all it can
find, so the build stops with `[with T = TheNewIntent]` and the sentence written
above. Nothing is left to a `default:` branch, and a case cannot be forgotten
quietly. The same shape does the work twice more:

- `Editor::handle<T>` answers a `ChangeIntent`, and the Editor is typed to that
  variant, so a view or a context instruction reaching the write path is not
  something it has to refuse at runtime - it is not a type it accepts.
- the visitors that name things - a node kind, a shape, a colour - are overload
  sets, so a new kind in the model is a build error until it is named too.

Two `switch`es are left in the code, both over an enum the model declares
(`Length::Unit`, `Kind`), where `-Wswitch` is what keeps them honest.

### The instructions

| instruction                               | what it means                                    | served by               |
| ----------------------------------------- | ------------------------------------------------ | ----------------------- |
| `tree`                                    | print the tree, with a shape's geometry and fill  | Model, viewing API      |
| `find <svg-id>`                           | the handle and the kind of one element            | Model, viewing API      |
| `create rect <id> <x> <y> <w> <h> [fill]` | a new rect, last under the root                   | Editor, one transaction |
| `create g <id>`                           | a new group, last under the root                  | Editor, one transaction |
| `delete <id>`                             | the node and everything under it                  | Editor, one transaction |
| `move <id> <dx> <dy>`                     | a shape's geometry, or a container's transform    | Editor, one transaction |
| `addto <gid> <id>`                        | the node, moved to the end of a container         | Editor, one transaction |
| `undo` / `redo`                           | one instruction back, or forward again            | Editor, the history     |
| `open <path>` / `save <path>`             | read or write the document's file                 | Model/FileSystem        |

A length is a number with an optional `px`, `pt`, `cm`, `mm`, `in`, `em`, `ex`, or
a percentage; a fill is `#rgb`, `#rrggbb`, `rgb(r, g, b)`, `none` or `currentColor`.
A bracketed value counts as one word, spaces and all, and quotes and escapes are
not parsed yet, so an SVG id cannot hold a space.

A length converts only inside its own system, and the two systems are never mixed.
mm and cm are metric, a power of ten apart, so 2cm + 5mm is 2.5cm - written in the
unit the value already had. in, pt and px are the lengths CSS defines against the
inch, 96px and 72pt to it, so they convert exactly among themselves. An inch is also
exactly 25.4mm, and 1mm + 1in is refused all the same: a document that allowed it
would hold 26.4mm, and whoever reads that value next cannot tell it was written in
two systems. A user unit, an em and an ex are in neither system - a user unit is
whatever the viewport's user coordinate system says, and an em or an ex is font
metrics - and a user unit is deliberately not a px: "1 user unit + 1 px = 2px" only
holds in a viewport whose user coordinate system happens to be one pixel wide, which
is a property of the viewport and not of the value.

## What happens inside

### Model: two APIs, one tree

Model shows two faces on purpose:

```C++
// viewing: never moves the history
Id root() const;
std::optional<Node> node(Id id) const;
std::vector<Id> children(Id parent) const;
std::optional<Id> find(std::string_view svg_id) const;

// editing: one user intent, one undoable block
Transaction transaction();
Id append_child(Id parent, Node payload);
void attach_last(Id parent, Id node);
void update(Id node, Node payload);
void erase(Id node);
void commit();  void rollback();

// history: the Model owns it, so the Planner never sees a diff id
void undo();  void redo();  bool can_undo() const;  bool can_redo() const;
```

A `Transaction` is an RAII handle. The ops are applied to the tree as they come,
and the destruction without `commit()` rolls the whole thing back, so an edit that
fails half way - a bad handle, a parser that refuses, an exception - leaves
nothing behind. That is also why the Editor is the only writer: undo and redo are
edits of the history, not of the tree, and they go through the same path.

### Model: the tree in a hive

The tree is stored in a `SwapDiffHive`, one hive for the node payloads and one for
the child links:

```C++
struct Edge { Id child_id; Id prev; };
struct NodeRecord { Node payload; Id parent; Id last_child_edge; };

SwapDiffHive<NodeRecord> nodes;
SwapDiffHive<Edge> edges;
```

The hive gives three ops - insert, remove, swap - and a history of them, so the
question the Model has to answer is how one user-level edit becomes a sequence of
those three. Three things make that work:

1. **The hive is the only memory.** A user edit never writes a payload other than
   through the hive, so every mutation is in the trace and `undo()` needs no
   bookkeeping of its own.
2. **A payload is never rewritten in place.** The hive moves payloads between
   slots, it does not track a slot's content, so an in-place field update would be
   invisible to a rollback. A patch is therefore *insert + swap + remove*: the new
   record takes the patched node's slot back and the record it displaced stays
   parked in the scratch slot, which is where the rollback restores it from. The
   patched slot is the same handle all along, so nothing that names a node has to
   be updated.
3. **One user intent is one diff id on both hives.** `Document` keeps the ids
   (`1, 2, 3, ...`, never reused) and opens the same transaction on both hives, so
   the block boundaries agree.

Walking the history is a `seek()` with an exact id rather than one block back per
hive, and that is not a detail: an edit that only touches the node hive - a payload
edit - adds a block to one hive and none to the other, and stepping both of them by
one block would desynchronize them the moment the two histories differ. `undo()`
and `redo()` both land on a recorded id, so both hives end up showing the same diff.

Each op of the transaction API is one shape of edit on top of those three hive
ops: `append_child` inserts a node and links it last, `attach_last` unlinks a node
and links it last somewhere else, `update` patches a payload and leaves the links
alone, `erase` unlinks a node and removes it with its subtree. The tree also
refuses what SVG does not allow: only a container or a text node takes children,
the root cannot be moved or erased, and a node cannot be moved inside its own
subtree.

Handles are slot ids. The slot of an erased node goes back to the free list and
the next insert takes it, so a handle can end up naming a different node than it
did before: a handle is a way to point at a node now, not a durable name. The
durable name of an element is its SVG id attribute, which is what `find()` walks
the tree for. The arena also hands out no way to list its slots, so the tree is
the only index there is - anything not reachable from the root is unreachable,
period.

### Structure

#### How to store a node

see `SVGNode.hh`

#### How to store links

```C++
struct Edge {
    Id child_id;
    Id prev;
};
```

We read a node's child by calling `edges[node.last_child_edge]` and recursively `i = edges[i].prev` until the chain ends at `no_id`.

Insert will go to last so that's O(1)

`no_id` is `std::numeric_limits<Id>::max()`, the value the hive calls `npos`.
Nothing is reserved in front of the first payload: the root is slot 0, and `no_id`
is where a link that leads nowhere points - the root's `parent`, a childless
node's `last_child_edge` and the first child's `prev` all read the same way. The
root is dropped from the history immediately, so it is the baseline and "undo
everything" lands on an empty document instead of no document.

The two names for that one value are a naming choice, not a difference: a second
global `npos` next to the hive's would be a redefinition in every Model source that
includes both. Model's sources assert the two agree, so the day the dependency
picks another value the build says so instead of quietly walking off the end of a
chain.

Inserting a child in the middle of a sibling list is not written yet. The link
layout above makes an append cheap on purpose; a middle insert has to patch the
`prev` of the successor link, which the same insert+swap+remove patch already
covers.

### Model: history and retention

The hive keeps an unbounded trace and offers `compact(trace_limit)` to drop what
is older than a limit. The Model does not expose that yet, and doing so is not a
one-liner: compaction is per hive, so a window that has to stay the same for both
of them is Model's decision to make, and it has to remember the floor it cannot
walk below.

### Model: ownership and layout

`Document` holds the arena, the diff ids and the history position as members, and
the two hives inside `TreeStore` are members of that. Nothing about a document sits
behind another pointer: a document and the tree it holds are built, moved and
destroyed as one object, and the only indirection left is the storage the hives
themselves own.

### Model: the transform

A node may carry a transform, and it is kept in the form SVG writes it:

```C++
struct Translate { LengthPercentage x, y; };
struct Scale { float x, y; };
struct Rotate { float degrees; };
struct Matrix { float a, b, c, d, e, f; };
using Transform = std::variant<Translate, Scale, Rotate, Matrix>;
```

The simple forms stay simple, because they say exactly what they mean and stay
exact under an edit; the matrix is what a transform becomes when a simple form can
no longer hold what it has to carry. `move` on a container is where that shows: the
first move writes a `translate`, a later move extends it, and a node that already
carries a scale or a rotation becomes a matrix with the translation in `e` and `f`
- which puts the translation in front of the transform the node had, so the node
moves in its parent's coordinate system, which is what moving it means. A matrix
counts in user units, so an offset that is not a plain number cannot go into one and
is refused rather than approximated. A rotation needs its cosine and sine to become
a matrix, which is the one place a matrix stops being exact.

A shape moves its own coordinates instead, since it has them: `move r 1 2` changes
`x` and `y` and leaves the transform alone. That keeps a shape's coordinates in the
units they were written with, at the price of the unit rule above - a rect whose `x`
is `50%` cannot be moved by a length, and a transform could.

### What is not there yet

- the SVG parser and serializer: `Document::open()` reads the file through
  `Model/FileSystem` and then reports that it cannot parse it yet, and
  `Document::save()` reports that it cannot serialize yet. Both are the seams
  where those two pieces go.
- the element kinds the Editor can create: a rect and a group, which is what can be
  built without a parser.
- the transform is in the model, but only `move` writes it: there is no `set
  transform` instruction, the parser does not read a `transform` attribute yet, and
  the serializer writes none of it.
- a `find()` index: the view walks the tree on every call, which cannot go stale
  and is cheap enough while the tree is small.
- the identity attributes: `NodeRecord` holds `parent` and `last_child_edge`,
  while `IdentityAttr::parent_id` and `ChildAttr::last_child_edge` still exist in
  the payload. The record is the authoritative copy today, which means the fields
  in the payload are dead weight - either drop them or let the arena read them.
- retention, compaction, and whether the whole-arena reset (`clear` /
  `clear_history`) deserves an instruction.
