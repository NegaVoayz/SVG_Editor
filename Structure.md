# SVG Editor Design

## Modules

| module              | what it is                                                        | depends on                    |
| ------------------- | ----------------------------------------------------------------- | ----------------------------- |
| `Model/`            | the SVG tree: where it is stored, how it is read, how it is edited  | MVCCHive                      |
| `Model/FileSystem/` | bytes in and out of the document's file                             | -                             |
| `Protocol/`         | the vocabulary the pipeline passes around, in the four parts it is made of: how a node is named, the intents, the views, the outcome | Model (for the values) |
| `Editor/`           | translates one change intent into tree transactions                 | Model, Protocol               |
| `Planner/`          | decides what one instruction means                                  | Model, Editor, Protocol       |
| `Ui/`               | the text front end: parses one line into an intent, prints the outcome; owns the document | Planner, Model, Protocol |

Three rules hold the shape together:

- **MVCCHive is Model's dependency.** The hive is the arena the tree is stored
  in: only `Model/` fetches it (`Model/cmake/MVCCHive.cmake`) and only `Model/`
  uses it - its transactions, commit ids and virtual ids are Model's business,
  and no other module opens one.
- **Everything above Model reaches the document through Model.** The Editor
  writes to it, the Planner reads it, and the Planner never talks to the Editor
  about anything but a change intent.
- **Text is the ui layer's business, in both directions of the boundary.** What a
  user typed becomes a typed intent before it leaves `Ui/`, so no layer above ever
  parses a string, and no layer below knows how an instruction was written.

Because `Document` holds the arena by value, the hive's headers do reach the other
modules' include path, so the link line no longer says who may use it. The test
named `layering` reads the sources of every module but Model and fails if one of
them names MVCCHive or `mvcc_hive`.

The text front end is also the composition root: it owns the `Document` and hands
it to the Editor and the Planner. A gui front end would sit in exactly the same
place, which is why the document is owned above the Planner rather than inside it.

## What happens after one instruction

> Here [M a b...] means this process mainly requires module b in a;

1. A line is read and parsed into a typed intent: a `Create` (holding a `CreateRect`, a
   `CreateCircle`, ...), a `Delete`, a `Read` - one of the alternatives of `Intent`. The
   values in it are model values by then - a `LengthPercentage`, a `Color`, a `NodeRef` - so
   nothing above this point sees a string a user typed. [M Ui] [M Protocol]
2. The Planner decides what that intent means, and that is the whole decision:
   [M Planner]
   - a **view** is served by reading the tree directly [M Model]
   - a **change** goes to the Editor, which translates it into a tree-level
     transaction [M Editor] [M Model]
   - a **context change** goes to the file layer, because it changes which
     document is being edited rather than the tree in it [M Model/FileSystem]
3. The outcome travels back the way it came: which branch produced it, and one of three
   structured answers - a `View` for a read, an `Event` saying what changed, or a
   `Refusal` saying why nothing changed. None of them carries user-facing prose; the ui
   layer turns the structure into words. [M Protocol] [M Ui]

A document starts absent: `Document::root()` is `no_id` until `init` makes one or
`read` loads one. The Planner answers those two without a document; every other
instruction, views included, is refused with
`error: no document. use 'init' or 'read' first`.

### What the protocol is made of

The vocabulary is one thing to the layers that speak it, and four things to a reader, so
`Protocol/` holds four headers and `Protocol.hh` is the name for all of them together:

| header       | what it is                                                                                 |
| ------------ | ------------------------------------------------------------------------------------------ |
| `NodeRef.hh` | how an instruction names a node - a handle, the root, an SVG id - and what that name turns out to be |
| `Intent.hh`  | the instructions and the three variants they form, the element instructions among them      |
| `View.hh`    | what a read hands back: the rows of a tree, or one node                                     |
| `Event.hh`   | what a successful change did: handles, values and the kind of edit                         |
| `Refusal.hh` | why an instruction could not be carried out: a closed set of typed reasons                 |
| `Outcome.hh` | `Kind`, and the `View` / `Event` / `Refusal` result                                        |

The parts depend on each other in that order, so there is no loop in the graph, and a file
includes the part it uses rather than the whole protocol: the umbrella is left for the two
places that drive the pipeline end to end, the cli and the test that drives it the same way.

An `Outcome` is one small sum type:

```C++
struct Outcome {
    Kind kind;                                   // view, change, or context
    std::variant<View, Event, Refusal> result;   // what, not how it reads
    bool ok() const;                             // a Refusal is the only failure
};
```

An `Event` names the handle that moved, the parent it moved under, the attribute that was
set, or the history position reached. A `Refusal` names the missing node, the duplicate id,
the two lengths that cannot be added, or the file that could not be read. A graphical front
end can react to those fields directly; it does not have to recover them from a sentence.
`Ui/src/Rendering.cc` is the only place that turns either one into English.

### How an intent is dispatched

An instruction is a `std::variant` of one struct per thing a user can ask for - except that
the six ways to create an element are one alternative, `Create`, which is a variant of its
own. Every layer that has to answer one does it with `std::visit` over that variant plus one
function per alternative. Most of those functions are not written by hand, because what
answers an instruction is a question about its type:

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
    if (!node) return refused(Kind::View, fault::MissingShape{intent.node});

    TreeView view;
    append_rows(view.rows, document_, *node, 0, true);
    return viewed(Kind::View, View{std::move(view)});
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

The Editor is the same shape with a different question asked of the type, and a create is
the one instruction it answers twice:

```C++
template<typename T>
Outcome handle(const T& intent) {
    if constexpr (requires { new_element(intent); }) {
        return create(intent);   // every element kind, one body
    } else {
        static_assert(false, "the Editor has no translation for this intent");
    }
}

// the second level: a create is one instruction, and its element kinds are the next question
template<> Outcome handle<Create>(const Create& intent) {
    return std::visit([this](const auto& element) { return handle(element); }, intent);
}
```

- The Editor is typed to `ChangeIntent`, so a view or a context instruction reaching
  the write path is not something it has to refuse at runtime - it is not a type it
  accepts.
- A create is answered in two levels. `Create` is one alternative of `ChangeIntent`, so
  level one routes it like any other change and the Planner never learns which element a
  line named; level two is a translation of its own, `handle<Create>` in
  `Editor/src/Create.cc`, which visits the kinds it may hold. Each kind comes back to the
  primary template above, where `new_element()` having an overload for it is what sends it
  to `create()` - so a kind added to `Create` with no builder is the build error naming
  that kind, and one with a builder needs no change to the routing above.
- One body answers all six kinds, and what differs is what the instruction brings: the
  overloads of `new_element()` are what each kind builds. The visit sits next to that body
  and the builders, so it is also what instantiates `create()` for every kind: the kinds
  are the alternatives of `Create`, and there is no second list of them to keep in step.
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
| `set <id> <attr> <value>`                        | one editable attribute of that node                    | the Editor, one transaction |
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

The verbs themselves are a table rather than a chain of tests: `Ui/src/Command.cc` holds one
reader per verb - the words of a line in, the instruction out - and the table of them is the
whole vocabulary of the front end. The first word of a line picks a row, so a new instruction
is a row and a reader and nothing else, and a word that is in no row is refused in the one
place that knows the table. `create`'s element kinds are a second table of that same shape in
`Ui/src/Create.cc`, and it is what builds the list in "create can build rect, circle, ellipse,
line, text or g, not 'poly'": the sentence is read out of the table, so it cannot fall behind
the kinds it names. The rest of the front end is one source per question it answers -
`IntentParser.cc` is the boundary and the command list, `Syntax.cc` is the shape a reader is
written in and what one word spells, `Rendering.cc` is the other direction of the boundary -
and the one pair that can still drift is the table and the help, so a test reads the table and
fails if the help does not name a verb in it.

`set` is the one instruction whose *attribute* is a runtime name, and it is typed in two
places so no layer below the front end parses a string. `Intent.hh` holds one struct per
attribute (`SetX`, `SetFill`, `SetTransform`, ...), grouped as the `Set` variant the same way
the element kinds are grouped as `Create`; each struct carries its own spelling as
`static constexpr name`. `Ui/src/Set.cc` is the table from that spelling to a reader, and the
reader is what decides the value's syntax - a length, a paint, a class name, a transform - so
`set box x 10` and `set box fill #00ff00` produce different typed instructions. The Editor
visits the attribute and writes the member its type names, asking each payload alternative
with `requires` whether the member is there; a node that does not carry it is refused with
`error: unknown attribute: <attr>`. No attribute is discovered by a switch over its name.
`id`, parent and child links are left out on purpose: a handle belongs to the hive, and where
a node sits is the structural vocabulary (`addto`, `front`, `back`, ...). `width`/`height`
mean the root's canvas or a rect's side, and `w`/`h` are the short spellings the tree prints.

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

A create reports the element as `kind#id`. The parent appears only when the instruction
named one: a default root insertion says `g#markers created`, and an explicit position
says `rect#box created in g#markers`.

```
svg> init
[context] an empty document, with the canvas it was given
svg> create g markers
[change] g#markers created
svg> create rect box 0 0 10 10 #ff0000 in markers
[change] rect#box created in g#markers
svg> create text title 20 40 "My Diagram"
[change] text#title created
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
and not the handle: a handle is a virtual id, which the hive never reuses for
another node.

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

// history: the Model owns it, so the Planner never sees a commit id
std::vector<Id> undo();  std::vector<Id> redo();
bool can_undo() const;  bool can_redo() const;

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
drop: the tree stays as it is and becomes the baseline. `init` goes
further: it creates the root when none exists, and otherwise replaces the whole
document. The canvas comes with the new root, and there is nothing of the old document
left to undo into, so it lands on the Model's `reset()` rather than in a transaction.

### Model: the tree in a hive

The tree is one `mvcc_hive::MVCCHive<Node>`. There are no side tables for parent
or child links: a `Node` is the whole record, and its place in the tree is part
of it.

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

struct Removed {  // the MVCC tombstone: a virtual id that has no document node
    Id parent_id = no_id;
    Id previous_sibling = no_id;
    Id next_sibling = no_id;
};

using Node = std::variant<SVGNode, PlainText, Removed>;

struct ChildAttr { Id first_child = no_id; Id last_child = no_id; };
```

A container or a text carries `ChildAttr`, a shape does not. The children of a
node are a doubly linked list: start at `first_child`, follow `next_sibling`, and
stop at `no_id`. A parent keeps both ends, so appending is O(1); a node keeps both
of its sibling links, so placing it in front of another child or removing it is
O(1) too. There is no backwards edge table to consult.

`Removed` is why a handle can come back without allocating a new id. Erasing a
subtree replaces each record with a `Removed` payload instead of removing the
row. The active checkpoint treats those rows as absent, while an older snapshot
still shows the original payloads. A later edit after an undo can therefore put
the same virtual id back with `replace`, and a handle that named a node before
the erase names the same node again when that checkpoint is selected.

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
- `insert_at` / `attach_at` do the same splice in front of a sibling. Because
  the list is doubly linked, the anchor's place is known without walking the
  level, and `no_id` still means the end.
- `update` replaces the payload and copies the existing place and child-list
  ends into the replacement, so a payload cannot bring another node's links
  with it.
- `erase_subtree` detaches the root of the subtree and replaces every record in
  it with `Removed`.

The tree refuses what SVG does not allow: only a payload with `ChildAttr` takes
children, the root cannot be moved or erased, a node cannot be moved inside its
own subtree, and an update cannot change whether a node holds children.

### Model: what one edit costs

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

History is the other axis. MVCCHive 1.0.2 keeps commit records and diffs in fixed-size
blocked vectors, so appending a commit no longer copies the retained commit log;
`TreeStore` grows its checkpoint list geometrically as well. The remaining history cost
is block reclamation: `VersionStore::drain_retirements()` calls `release_empty_blocks()`,
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
scaling linearly. The parser should commit a large load in chunks and clear the history
when the load is finished.

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

### Model: history, snapshots and checkpoints

`TreeStore` keeps a `Snapshot` for each user-level commit, and `Document`
undoes by selecting an earlier snapshot. The tree is not rewritten to undo:
the snapshot already holds the immutable versions that were visible then.

The one place where that is not enough is an edit made after an undo. MVCCHive
transactions extend the head and cannot branch from an old commit, so before
opening the new transaction `TreeStore` rewinds the head to the visible
checkpoint. It reads the normalized diffs between the two commits, and for each
touched virtual id writes the visible payload back, or a `Removed` record when
the visible checkpoint did not have that id. That rewind is one commit; the new
edit follows it, the redo branch is dropped, and the checkpoints before the
visible one stay usable. In other words, undo is snapshot navigation, and
editing after undo is a new branch expressed as a head-to-visible catch-up
followed by the edit.

`clear_history()` first makes the head match the visible checkpoint, then asks
the hive to prune its older commits and keeps the current head as the baseline.
`reset()` is stronger: it drops every snapshot, replaces the whole hive
generation, and builds a new root. A document that holds that hive by value
cannot be moved, which is why `Document` is non-copyable and non-movable.

The number of checkpoints is the history depth. The hive's own retention
controls (`prune_before`, `clear_history`) are exposed only through those
checkpoint operations; no other module sees a commit id.

Undo and redo return the virtual ids whose records differ across the two
checkpoints. `TreeStore` gets them from the hive's `diffs(commit_id)` for the
commit that produced the newer checkpoint, and maps each `Diff::virtual_id`
to an `Id`. The list includes neighbours whose links changed, not only the
node an instruction named, and a deleted subtree appears as every tombstoned
id. The cost is proportional to that touched set: the hive copies its 32-byte
diff entries, then the document keeps an 8-byte id for each one. The snapshot
switch itself stays O(1); the ids are the price of telling a front end what to
refresh.

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
- the transform is in the model, and `move`, `scale` and `set transform` write it; the
  parser does not read a `transform` attribute from a file yet, and the serializer writes
  none of it.
- a `find()` index: the view walks the tree on every call, which cannot go stale
  and is cheap enough while the tree is small.
- retention: the hive offers `prune_before` and `clear_history`, and `TreeStore` uses
  the latter at checkpoints, but a document with many undo steps keeps one snapshot
  guard per step. Removed records also stay as `Removed` rows until a reset, because
  an older checkpoint may still show them. A retention policy that drops old
  checkpoints and then prunes the hive is the next piece; it is not exposed yet.
- escaping in the front end: a quoted run is one word and there is no way to put a quote
  inside one, so a text cannot spell `"` yet.
