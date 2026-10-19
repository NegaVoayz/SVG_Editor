# SVG Editor Pipeline

The path from one line of text to one structured outcome: the protocol, the dispatch,
the command vocabulary, and what a session looks like. The module map and dependency
rules are in [Structure.md](Structure.md); the tree behind the views and transactions is
in [Model.md](Model.md).

## What the protocol is made of

The vocabulary is one thing to the layers that speak it, and six things to a reader, so
`Protocol/` holds six headers and `Protocol.hh` is the name for all of them together:

| header       | what it is                                                                                 |
| ------------ | ------------------------------------------------------------------------------------------ |
| `NodeRef.hh` | how an instruction names a node - a handle, the root, an SVG id - and what that name turns out to be |
| `Intent.hh`  | the instructions and the nested variants they form (`Create`, `Set`, `Intent`, `ChangeIntent`) |
| `View.hh`    | what a read hands back: the rows of a tree, or one node                                     |
| `Event.hh`   | what a successful change did: user-facing element names, values and the kind of edit       |
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

An `Event` names the element that moved, the parent it moved under, the attribute that was
set, or the history position reached. An element's name is its SVG tag and id, not the
hive's virtual handle. A `Refusal` names the missing node, the duplicate id, the two lengths
that cannot be added, or the file that could not be read. A graphical front end can react
to those fields directly; it does not have to recover them from a sentence.
`Ui/src/Rendering.cc` is the only place that turns either one into English.
An `Undone` or `Redone` event also carries the original structured edit event when the
Editor recorded one, alongside the nodes whose state changed during the history move.
The text front end prints it as `undo: <original edit>` or `redo: <original edit>`.
The event also carries the commit id of the checkout it landed on. The Editor commits
explicitly with one shared id, so every participating hive's native checkout reports
that same value. The Editor keeps one entry per history position in a `std::deque`,
with the commit id stored beside the outcome and checked when the outcome is replayed.

## How an intent is dispatched

The `variant + visit + handle<T>` shape described in [Structure.md](Structure.md) is the
project's central structural mechanism, not a local convenience.

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
- The other translations are written where they are routed, one file per thing an edit
  changes: `Editor/src/Create.cc` builds elements, `Structure.cc` moves nodes and levels
  around, `Payload.cc` changes what one node holds, `Set.cc` changes one named attribute,
  and `History.cc` is undo and redo. `Editor/src/Translations.hh` declares them and defines nothing, because that
  declaration is load-bearing: a specialization the compiler has not met before the point
  where `std::visit` instantiates `handle<T>` is not a specialization at all, and the
  primary template's build error is what comes out instead.
- The arithmetic underneath those is split by question: Model's `Units.cc` is what a
  length is worth and what it may be added to, and the Editor's `Geometry.cc` is what a
  move or a scale does to one node's params. Both a param edit and a transform matrix use
  the same unit table. The unit table is the one part that is about values rather than
  about nodes, which is why its interface needs nothing beyond `LengthPercentage`.
- the visitors that name things - a node kind, a shape, a colour - are overload
  sets, so a new kind in the model is a build error until it is named too.

The `switch`es that are left are all over an enum the model declares (`Length::Unit`,
`Kind`), where `-Wswitch` is what keeps them honest: a new enum value is a warning
until every one of them names it.

## The instructions

| instruction                                      | what it means                                          | answered by             |
| ------------------------------------------------ | ------------------------------------------------------ | ----------------------- |
| `tree [id]`                                      | the tree, the whole document by default                | the Planner, Model's viewing API |
| `show <id>`                                      | one element's attributes and its parent                | the Planner, Model's viewing API |
| `history [n]`                                    | the last n edits with their commit ids, all by default | the Editor's replay log |
| `canvas`                                         | the document's size, which is the root element's       | the Planner, Model's viewing API |
| `help`                                           | the command list                                       | the front end itself    |
| `create rect <id> <x> <y> <w> <h> [fill]`        | a rectangle                                            | the Editor, one transaction |
| `create circle <id> <cx> <cy> <r> [fill]`        | a circle                                               | the Editor, one transaction |
| `create ellipse <id> <cx> <cy> <rx> <ry> [fill]` | an ellipse                                             | the Editor, one transaction |
| `create line <id> <x1> <y1> <x2> <y2> [stroke]`  | a line, which has no inside to fill                    | the Editor, one transaction |
| `create text <id> <x> <y> "content"`             | a text element and the character data inside it        | the Editor, one transaction |
| `create g <id>`                                  | a group                                                | the Editor, one transaction |
| `delete <id>`                                    | the node and its descendants                           | the Editor, one transaction |
| `move <id> <dx> <dy> [transform\|params] [cascade\|restrict]` | a node's params or transform, or a container's descendants | the Editor, one transaction |
| `scale <id> <factor> [transform\|params] [cascade\|restrict]` | a node's params or transform, or a container's descendants | the Editor, one transaction |
| `set <id> <attr> <value>`                        | one editable attribute of that node                    | the Editor, one transaction |
| `group <newGid> <id> <id> ...`                   | a new group, standing where the first node stood        | the Editor, one transaction |
| `ungroup <gid>`                                  | the children, back in the group's place                | the Editor, one transaction |
| `addto <gid> <id>`                               | the node, as the last child (the front) of a container  | the Editor, one transaction |
| `front <id>` / `back <id>`                       | the two ends of the child order, last child in front    | the Editor, one transaction |
| `undo` / `redo` / `checkout <commit>`            | one instruction back, forward again, or to a commit    | the Editor, the history |
| `canvas <w> [h]`                                 | a new size, and a checkpoint                           | the Editor, the history |
| `init [w] [h]`                                   | a fresh document, 800x600 by default                   | the Planner, Model      |
| `read <file.svg>`                                | read a document                                        | the Planner, Model/FileSystem |
| `save [file.svg]`                                | write it, where it was read from by default            | the Planner, Model/FileSystem |

Every `create` also takes `in <gid>`, which is where the new element goes; without it
the element is the last child of the root, because an element that belonged to nothing
would be unreachable and unreclaimable. `help` is the one instruction that never
leaves the front end: what the commands are is the syntax of that layer, and no other
layer knows it or needs to.

`init`, `read`, `save`, and `canvas` set an IO baseline. Undo at that baseline is
refused with `error: undo not allowed after io command`; ordinary edits after it remain
undoable.

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
For a text element, `set <id> text "<words>"` edits the PlainText child; `content` remains
accepted as the earlier spelling of that same edit.
`id`, parent and child links are left out on purpose: a handle belongs to the hive, and where
a node sits is the structural vocabulary (`addto`, `front`, `back`, ...). `width`/`height`
mean the root's canvas or a rect's side, and `w`/`h` are the short spellings the tree prints.
`xmlns` and `viewBox` belong to the root `<svg>`; `text-anchor` (`start`, `middle`, `end`)
and `font-size` belong to text nodes, including `tspan` and `textPath`.

`front` and `back` follow SVG's paint order: the last child is painted last, so it is
the front, and the first child is the back. The tree prints children in that same chain
order, so a later line is nearer the front.

`move` and `scale` take two optional selectors. `transform` pre-multiplies the node's
transform matrix and leaves its params alone. A transform is a field of every SVG node,
so this is accepted for containers too; the edit touches the container record and leaves
its descendants untouched. `params` changes values directly. `restrict` edits only the node
itself: a leaf's params, or a container's own transform. `cascade` edits descendants; on a
non-container the only node in that subtree is the node itself, so it behaves as a params
edit. With neither selector, the defaults are `params` and `cascade`.
`Editor/src/Payload.cc` resolves the node kind, collects a cascade's changed payloads
before opening a transaction, and commits them as one undo step. A node that already
carries a transform is a boundary, whether it is the node the instruction named or a
descendant a cascade reached: writing a parent-space delta into its params would be read
in its local coordinate system and move it by the wrong amount. The operation is
pre-multiplied into that node's matrix instead, and a cascade leaves its subtree alone
because the new matrix already covers it. A transformed node therefore takes a default or
explicit `params` move or scale in its transform. Every `move` answer uses
`<target> moved by (x, y)`; a cascade adds the number of changed elements.
An untransformed node still changes its params. `event::Moved`/
`event::Scaled` carry the resolved mode and the number of nodes whose params or transform
the edit changed.

When `scale` edits params, it scales every drawn dimension on the leaf: shape geometry,
text position, offsets and font size, and `stroke-width`. A transform-based scale reaches
the stroke through the transform instead.

A container with no descendants is refused for both `restrict` and `cascade`:
`fault::EmptyContainer` becomes
`error: cannot move or scale an empty container: g#node2`. The edit would change nothing
visible now but would move future children, which is exactly the confusing state the
refusal prevents.

A length is a number with an optional `px`, `pt`, `cm`, `mm`, `in`, `em`, `ex`, or a
percentage; a fill is `#rgb`, `#rrggbb`, `rgb(r, g, b)`, a CSS named or system colour,
`none` or `currentColor`. A bracketed value counts as one word,
spaces and all, and so does a quoted run - which is what lets a text hold spaces.
Inside a quoted run, `\"` writes a quote and `\\` writes a backslash.
A line whose first non-space character is `#` is a comment and runs to the end of the
line, including any semicolons in it.

A canvas side must be positive: `init -5 -5` and `canvas 0` are refused. A shape
dimension or radius may be zero, as SVG allows, but a negative value is refused.
Printed floats use at most six significant figures.

`read` reports the successful load first, then one exact warning line for each id it
had to invent:

```text
read diagram.svg (12 elements)
warning: assigned id rect1 to unnamed <rect>
warning: assigned id circle1 to unnamed <circle>
```

The ids are generated per element kind in document order and never reuse an explicit
id from anywhere in the file. The root is left unnamed when the source left it unnamed.
Malformed XML, an unsupported element or attribute, an invalid value, duplicate ids and
text outside a text element are structured refusals; a failed read leaves the previous
document and source path untouched.

`save` reports the destination and the same non-root element count:
`saved to diagram.svg (12 elements)`.

`ungroup` works below the root: a group that is a direct child of `<svg>` is a
top-level container and is refused as such, even when it is empty. A childless group
deeper in the tree can be ungrouped; it is simply removed.

## What a session looks like

The prompt is `svg> `, one line may hold several instructions separated by `;`, and a
view is printed as the thing it describes rather than as a sentence about it. Anything
else is the sentence it already is. Answers carry no route marker because the result
already names the operation, and a refusal starts with `error:`.

A create reports the element as `kind#id`. The parent appears only when the instruction
named one: a default root insertion says `g#markers created`, and an explicit position
says `rect#box created in g#markers`.

```
svg> init
canvas 800x600 created
svg> create g markers
g#markers created
svg> create rect box 0 0 10 10 #ff0000 in markers
rect#box created in g#markers
svg> create text title 20 40 "My Diagram"
text#title created
svg> tree
svg (800x600)
├── g#markers
│   └── rect#box  fill=#ff0000 x=0 y=0 w=10 h=10
└── text#title    (20,40) text="My Diagram"
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
  of the model, so `text#title (20,40) text="My Diagram"` is one row of the view and two
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

