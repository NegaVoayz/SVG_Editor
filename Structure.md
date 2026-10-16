# SVG Editor Design

## Modules

| module              | what it is                                                        | depends on                    |
| ------------------- | ----------------------------------------------------------------- | ----------------------------- |
| `Model/`            | the SVG tree: where it is stored, how it is read, how it is edited  | SwapDiffHive                  |
| `Model/FileSystem/` | bytes in and out of the document's file                             | -                             |
| `Protocol/`         | the vocabulary the pipeline passes around, in the four parts it is made of: how a node is named, the intents, the views, the outcome | Model (for the values) |
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

1. A line is read and parsed into a typed intent: a `CreateRect`, a `Delete`, a
   `Read`, one of the alternatives of `Intent`. The values in it are model values
   by then - a `LengthPercentage`, a `Color`, a `NodeRef` - so nothing above this
   point sees a string a user typed. [M Ui] [M Protocol]
2. The Planner decides what that intent means, and that is the whole decision:
   [M Planner]
   - a **view** is served by reading the tree directly [M Model]
   - a **change** goes to the Editor, which translates it into a tree-level
     transaction [M Editor] [M Model]
   - a **context change** goes to the file layer, because it changes which
     document is being edited rather than the tree in it [M Model/FileSystem]
3. The outcome travels back the way it came: whether it worked, which branch produced
   it, a sentence about what happened, and - when what was asked for was a read - what
   the read found, as data. [M Protocol] [M Ui]

### What the protocol is made of

The vocabulary is one thing to the layers that speak it, and four things to a reader, so
`Protocol/` holds four headers and `Protocol.hh` is the name for all of them together:

| header       | what it is                                                                                 |
| ------------ | ------------------------------------------------------------------------------------------ |
| `NodeRef.hh` | how an instruction names a node - a handle, the root, an SVG id - and what that name turns out to be |
| `Intent.hh`  | the instructions and the two variants they form                                             |
| `View.hh`    | what a read hands back: the rows of a tree, or one node                                     |
| `Outcome.hh` | `Kind`, `Outcome`, and the one shape a refusal takes                                        |

The parts depend on each other in that order, so there is no loop in the graph, and a file
includes the part it uses rather than the whole protocol: the umbrella is left for the two
places that drive the pipeline end to end, the cli and the test that drives it the same way.

### How an intent is dispatched

An instruction is a `std::variant` of one struct per thing a user can ask for, and
every layer that has to answer one does it with `std::visit` over that variant plus
one function per alternative. Most of those functions are not written by hand,
because what answers an instruction is a question about its type:

```C++
// the Planner. Every change instruction goes to the Editor, and which instructions
// those are is what ChangeIntent holds, so the compiler answers it from the type.
template<typename T>
Outcome handle(const T& intent) {
    if constexpr (std::is_constructible_v<ChangeIntent, T>) {
        return editor_.translate(ChangeIntent{intent});
    } else {
        static_assert(false, "the Planner has no route for this intent");
    }
}

// the five instructions that are not the Editor's, each written where it is routed
template<> Outcome handle<ShowTree>(const ShowTree& intent) {
    const std::optional<Id> node = look_up(document_, intent.node);
    if (!node) return Outcome{false, Kind::View, "there is no node at " + describe(intent.node)};

    TreeView view;
    append_rows(view.rows, document_, *node, 0, true);
    return Outcome{true, Kind::View, {}, std::move(view)};
}
template<> Outcome handle<Init>(const Init& intent) { ... }
```

The point is what happens when an instruction is added, and it is the same in both
layers: the variant grows, the visitor has to answer the new alternative, and what it
finds is either a route that already answers it by type or the build error above -
`In instantiation of 'handle(const T&) [with T = TheNewIntent]'` and the sentence
written next to the `static_assert`, which is what says whose case is missing.
Nothing is left to a `default:` branch, and a case cannot be forgotten quietly.

The `static_assert(false, ...)` in a discarded branch of an `if constexpr` is C++23
(P2593): before it, a false assertion in a template definition was ill-formed whether or
not the branch it sat in was ever taken, and a dependent `always_false<T>` was the way to
get around that. Both layers here want the same thing - a branch that cannot be reached by
a case that has a route, and that a case without one must take - so neither of them needs
the workaround now, and neither of them has one.

The Editor is the same shape with a different question asked of the type:

```C++
template<typename T>
Outcome handle(const T& intent) {
    if constexpr (requires { new_element(intent); }) {
        return create(intent);   // every element kind, one body
    } else {
        static_assert(false, "the Editor has no translation for this intent");
    }
}
```

- The Editor is typed to `ChangeIntent`, so a view or a context instruction reaching
  the write path is not something it has to refuse at runtime - it is not a type it
  accepts.
- A create is answered by one body for all six element kinds, and what differs is what
  the instruction brings: the overloads of `new_element()` are the list of kinds, and
  the explicit instantiations of `create()` at the end of the source are the same list
  written for the compiler. A kind with a builder is routed; a kind without one, or an
  instruction that brings no element at all and has no translation of its own, is a
  build error naming its type.
- The other eleven translations are written where they are routed, one file per thing an
  edit changes: `Editor/src/Create.cc` builds elements, `Structure.cc` moves nodes and
  levels around, `Payload.cc` changes what one node holds, and `History.cc` is undo and
  redo. `Editor/src/Translations.hh` declares them and defines nothing, because that
  declaration is load-bearing: a specialization the compiler has not met before the point
  where `std::visit` instantiates `handle<T>` is not a specialization at all, and the
  primary template's build error is what comes out instead.
- The arithmetic underneath those is three sources, one question each: `Units.cc` is what a
  length is worth and what it may be added to, `Transform.cc` is what a transform does with
  a value it has to carry, and `Geometry.cc` is what a move or a scale does to one node.
  The unit table is the one part of it that is about values rather than about nodes, which
  is why it is the one part with no model type in its interface beyond `LengthPercentage`.
- the visitors that name things - a node kind, a shape, a colour - are overload
  sets, so a new kind in the model is a build error until it is named too.

The `switch`es that are left are all over an enum the model declares (`Length::Unit`,
`Kind`), where `-Wswitch` is what keeps them honest: a new enum value is a warning
until every one of them names it.

### The instructions

| instruction                                      | what it means                                          | answered by             |
| ------------------------------------------------ | ------------------------------------------------------ | ----------------------- |
| `tree [id]`                                      | the tree, the whole document by default                | the Planner, Model's viewing API |
| `show <id>`                                      | one element's own attributes                           | the Planner, Model's viewing API |
| `canvas`                                         | the document's size, which is the root element's       | the Planner, Model's viewing API |
| `help`                                           | the command list                                       | the front end itself    |
| `create rect <id> <x> <y> <w> <h> [fill]`        | a rectangle                                            | the Editor, one transaction |
| `create circle <id> <cx> <cy> <r> [fill]`        | a circle                                               | the Editor, one transaction |
| `create ellipse <id> <cx> <cy> <rx> <ry> [fill]` | an ellipse                                             | the Editor, one transaction |
| `create line <id> <x1> <y1> <x2> <y2> [stroke]`  | a line, which has no inside to fill                    | the Editor, one transaction |
| `create text <id> <x> <y> "content"`             | a text element and the character data inside it        | the Editor, one transaction |
| `create g <id>`                                  | a group                                                | the Editor, one transaction |
| `delete <id>`                                    | the node and everything under it                       | the Editor, one transaction |
| `move <id> <dx> <dy>`                            | a shape's geometry, or a container's transform         | the Editor, one transaction |
| `scale <id> <factor>`                            | a shape's own values, or a container's transform       | the Editor, one transaction |
| `group <newGid> <id> <id> ...`                   | a new group, standing where the first node stood        | the Editor, one transaction |
| `ungroup <gid>`                                  | the children, back in the group's place                | the Editor, one transaction |
| `addto <gid> <id>`                               | the node, as the last child of a container             | the Editor, one transaction |
| `front <id>` / `back <id>`                       | the two ends of the child order, first child in front   | the Editor, one transaction |
| `undo` / `redo`                                  | one instruction back, or forward again                 | the Editor, the history |
| `canvas <w> [h]`                                 | a new size, and a checkpoint                           | the Editor, the history |
| `init [w] [h]`                                   | a fresh document, 800x600 by default                   | the Planner, Model      |
| `read <file.svg>`                                | read a document                                        | the Planner, Model/FileSystem |
| `save [file.svg]`                                | write it, where it was read from by default            | the Planner, Model/FileSystem |

Every `create` also takes `in <gid>`, which is where the new element goes; without it
the element is the last child of the root, because an element that belonged to nothing
would be unreachable and unreclaimable. `help` is the one instruction that never
leaves the front end: what the commands are is the syntax of that layer, and no other
layer knows it or needs to.

`front` and `back` are the two ends of the child order, and the first child is what this
calls the front - the order the tree prints in is the order they move a node through.
That is the one place where the vocabulary and SVG disagree: SVG paints what comes later
over what came earlier, so a viewer draws the *last* child on top. If the tree is ever to
mean SVG's z-order rather than its own line order, those two names are the ones that
would have to swap.

A length is a number with an optional `px`, `pt`, `cm`, `mm`, `in`, `em`, `ex`, or a
percentage; a fill is `#rgb`, `#rrggbb`, `rgb(r, g, b)`, `none` or `currentColor`, and
named colours have no ids in the model yet. A bracketed value counts as one word,
spaces and all, and so does a quoted run - which is what lets a text hold spaces.
Escaping is still missing, so a quote inside a quoted run cannot be written.

### What a session looks like

The prompt is `svg> `, one line may hold several instructions separated by `;`, and a
view is printed as the thing it describes rather than as a sentence about it. Anything
else is the sentence it already is, with the branch that answered it in front:

```
svg> create g markers
[change] created #1 <g> id="markers" in the root
svg> create rect box 0 0 10 10 #ff0000 in markers
[change] created #2 <rect> id="box" in "markers"
svg> create text title 20 40 "My Diagram"
[change] created #3 <text> id="title" in the root
svg> tree
svg (800x600)
├── g#markers
│   └── rect#box  x=0 y=0 w=10 h=10 fill=#ff0000
└── text#title    (20,40) "My Diagram"
```

Three things about the drawing are worth knowing, because each of them is a decision:

- **The first line is the row the view is rooted at**, printed without box drawing, and
  the rows under it hang from `├──`, `└──` and the bars of the ancestors that had a
  sibling after them. `tree` roots the view at the document, which is why the first
  line is the `<svg>` element and its canvas; `tree g1` roots it at that group.
- **The attributes line up in one column**, two places past the longest name in the
  view - with the box drawing in front of it counted, which is why a deeper line starts
  further right and the column is still one column. The first line is not padded: there
  is no column in front of it to line up with. The width is counted in places a reader
  sees, not in bytes, because every box character is one place and three bytes.
- **A text's words are printed inside the element that holds them**, and not as a line
  of their own, which is what makes a text read as a text: the character data is a node
  of the model, so `text#title (20,40) "My Diagram"` is one row of the view and two
  nodes of the tree.

What a name is, is the kind and the SVG id the user wrote - `rect#box`, `g#markers` -
and not the handle: a handle is a slot number, and a slot can name another node once
the node in it is erased.

A length converts only inside its own system, and the two systems are never mixed.
mm and cm are metric, a power of ten apart, so 2cm + 5mm is 2.5cm - written in the
unit the value already had. in, pt and px are the lengths CSS defines against the
inch, 96px and 72pt to it, so they convert exactly among themselves. An inch is also
exactly 25.4mm, and 1mm + 1in is refused all the same: a document that allowed it
would hold 26.4mm, and whoever reads that value next cannot tell it was written in
two systems. The boundary is the system and not the unit, so 1cm + 1in is refused for
the same reason, and the refusal says which side is which: "1in is imperial and 1cm is
metric". A number that was added up by a program is one nobody can take apart again,
and the sentence is what says so.

A user unit, an em and an ex are in neither system - a user unit is
whatever the viewport's user coordinate system says, and an em or an ex is font
metrics - and a user unit is deliberately not a px: "1 user unit + 1 px = 2px" only
holds in a viewport whose user coordinate system happens to be one pixel wide, which
is a property of the viewport and not of the value.

## What happens inside

### Model: two APIs, one tree

Model shows two faces on purpose:

```C++
// viewing: never moves the history, and never writes
Id root() const;
std::optional<Node> node(Id id) const;
std::vector<Id> children(Id parent) const;
std::optional<Id> parent(Id node) const;
std::optional<Id> find(std::string_view svg_id) const;

// editing: one user intent, one undoable block
Transaction transaction();
Id append_child(Id parent, Node payload);
void attach_last(Id parent, Id node);
Id insert_at(Id parent, Id before, Node payload);   // in front of a sibling; no_id is the end
void attach_at(Id parent, Id before, Id node);
void update(Id node, Node payload);
void erase(Id node);
void commit();  void rollback();

// history: the Model owns it, so the Planner never sees a diff id
void undo();  void redo();  bool can_undo() const;  bool can_redo() const;

// the context: which document this is, rather than what is in it
const std::string& source_path() const;
Status open(std::string_view path);                  // and reset, when the parser exists
Status save(std::optional<std::string_view> path);   // no path: where it was read from
void reset(std::optional<LengthPercentage> width, std::optional<LengthPercentage> height);
void clear_history();                                // the tree stays and becomes the baseline
```

A `Transaction` is an RAII handle. The ops are applied to the tree as they come,
and the destruction without `commit()` rolls the whole thing back, so an edit that
fails half way - a bad handle, a parser that refuses, an exception - leaves
nothing behind. That is also why the Editor is the only writer: undo and redo are
edits of the history, not of the tree, and they go through the same path.

### Model: the canvas, and the checkpoints

The canvas is not a field of the document: it is the root `<svg>` element's own `width`
and `height`, which is where SVG writes it and what a serializer will write it back out
of. `canvas <w> [h]` is therefore an ordinary edit of one payload - the root's - and
`tree` reads the size off the root row like anything else about an element, which is why
`canvas` with no operand is exactly the read `show /` is. The default is 800x600 in user
units, which is the bare-number form SVG writes and what the front end shows before
anything is set.

Three instructions drop the history: `canvas`, `save`, and `init`, which replaces the
document instead of editing it. What the first two have in common is that the tree they
leave behind is the state the file, or the frame behind the drawing, says it is - so the
way back to the state before them is a state nobody wrote down. `clear_history()` is that
drop: the tree stays as it is and becomes the baseline. It has to reach both hives in one
call, or the pair of them would disagree about where the beginning is. `init` goes
further: the canvas comes with the new root, and there is nothing of the old document
left to undo into, so it lands on the Model's `reset()` rather than in a transaction.

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

A child can also be placed in front of a sibling, and that is the same splice wherever
the sibling sits: the chain runs backwards, so the new link takes the anchor's place in
the backwards walk and points at what the anchor pointed at. Placing in front of the
first child is therefore nothing special, and only the end is cheaper - which is the one
`append_child()` takes. Three instructions are built out of that one splice: `front`
places a node in front of what is currently the first child, `group` places the new group
in front of the first sibling after the node it takes that is not being taken itself, and
`ungroup` places each child in front of the node that stood after the group, in order -
which keeps their order and leaves the level where it was.

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

`scale` lands the same way round: a shape's own values are multiplied where they stand,
each in the unit it was written in, so a scale never asks the unit table anything, and a
container takes the scale in its transform. That scale goes *behind* what the node
already carries - `translate(...) scale(...)` - which leaves the node where a move put it
and grows what is drawn under it from the node's own origin; it is the same drawing that
multiplying every coordinate under the node would leave, in one payload instead of all of
them. What the transform already is decides how it takes the factor: a scale stays a
scale, a matrix takes it in the four linear entries and keeps its translation, a rotation
becomes its cosine and sine - the one place a matrix stops being exact - and a translate
has to become a matrix, which counts in user units, so a translate written in a unit
refuses the scale instead of approximating it. A factor of nothing, or a negative one,
is refused for the same reason: SVG can mirror and collapse, and nothing here writes
either yet.

### What is not there yet

- the SVG parser and serializer: `Document::open()` reads the file through
  `Model/FileSystem` and then reports that it cannot parse it yet, and
  `Document::save()` reports that it cannot serialize yet. `read` and `save` are wired
  to them, path memory and all, so both are seams with nothing behind them rather than
  instructions still to write; `read` has to reset the document and name the elements it
  finds unnamed, and the drawing of those warnings is the front end's to print.
- the element kinds the Editor can create are the four shapes, a text and a group, which
  is what can be built without a parser: the model has no `path` at all, and a `defs`, a
  `symbol` or a `switch` can only arrive from one.
- named colours: the model has ids for the system colours and none for the named ones,
  so `fill=red` is not a fill this reads yet. That is why the drawing prints `#ff0000`.
- the transform is in the model, but only `move` and `scale` write it: there is no `set
  transform` instruction, the parser does not read a `transform` attribute yet, and the
  serializer writes none of it.
- a `find()` index: the view walks the tree on every call, which cannot go stale
  and is cheap enough while the tree is small.
- the identity attributes: `NodeRecord` holds `parent` and `last_child_edge`,
  while `IdentityAttr::parent_id` and `ChildAttr::last_child_edge` still exist in
  the payload. The record is the authoritative copy today, which means the fields
  in the payload are dead weight - either drop them or let the arena read them.
- retention and compaction, now that `clear_history()` is what three instructions reach
  for: the hives offer `compact(trace_limit)`, and a document that only ever grows its
  trace is the other half of that question.
- escaping in the front end: a quoted run is one word and there is no way to put a quote
  inside one, so a text cannot spell `"` yet.
