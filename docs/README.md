# SVG Editor

An SVG editor built as a small pipeline: the text front end parses an instruction
into a typed intent, and the Planner decides what it means. Model serves views, the
Editor serves tree edits, and FileSystem serves file and context operations. The
design is split by role: [Structure.md](Structure.md) is the overview and dependency map,
[Pipeline.md](Pipeline.md) covers the vocabulary and command path, and
[Model.md](Model.md) covers the tree, history and storage costs.

## Requirements

- A C++23 toolchain. That is the language this project is written in;
  [MVCCHive](https://github.com/NegaVoayz/MVCCHive) is written in C++23 too.
  P2593 - a `static_assert(false, ...)` in a discarded branch of an `if constexpr`,
  which is what makes an instruction with no handler a build error here - is one of the
  C++23 features the code relies on rather than merely benefits from, so the floor is
  GCC 13 or Clang 17 and up.
- CMake 3.28 or newer, which is the floor MVCCHive itself declares.

## Building

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The first configure fetches MVCCHive and tinyxml2 at the versions pinned in
`Model/cmake/` and `FileSystem/cmake/`. A package config found by `find_package` or a checkout pointed at by the
matching `FETCHCONTENT_SOURCE_DIR_*` variable avoids a download:

```sh
# uses ../MVCCHive as it is; tinyxml2 still needs a package or local source
cmake --preset local-dep
cmake --build --preset local-dep
ctest --preset local-dep
```

`release` is the same as `default` with `CMAKE_BUILD_TYPE=Release`. To move the
dependency instead of taking it from next door:

```sh
cmake --preset default \
  -DSVG_EDITOR_MVCCHIVE_REPOSITORY=https://host/mirror/MVCCHive.git \
  -DSVG_EDITOR_MVCCHIVE_TAG=9b7501802f087c419fcb8f8b42430547fe4d5087 \
  -DSVG_EDITOR_TINYXML2_REPOSITORY=https://host/mirror/tinyxml2.git \
  -DSVG_EDITOR_TINYXML2_TAG=11.0.0
```

## Running

```sh
build/ninja-local-dep/Ui/svg-editor.exe     # the prompt, one instruction per line
build/ninja-local-dep/Ui/svg-editor.exe "init; create g markers; create rect box 0 0 10 10 #ff0000 in markers; tree"
```

The interactive prompt starts with `svg-editor <version> — type 'help' for commands`.
The prompt is `svg>`, `help` lists the commands, and one line may hold several
instructions separated by `;`. A view is printed as what it describes; a change comes
back as a structured event, and a refusal as a typed reason, both rendered here by the
ui layer. A line whose first non-space character is `#` is a comment and runs to the end
of the line. Quoted text accepts `\"` and `\\`. `transform=""`, a whitespace-only
transform value and `transform="none"` all mean the identity transform. A session starts
without a document:

A create reports the element as `kind#id`. It reports the parent only when the command
named one: a default root insertion is `g#markers created`, while an explicit position is
`rect#box created in g#markers`.
`init` makes an empty one, and `read` makes one from a file. Before either, every
pipeline command is refused with `error: no document. use 'init' or 'read' first`;
`help` is answered by the front end itself and still works.

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
svg (800 x 600 user units)
├── g#markers
│   └── rect#box  fill=#ff0000 x=0 y=0 w=10 h=10
└── text#title    (20,40) text="My Diagram"
```

The instructions are:

| what                                            | what it does                                              |
| ----------------------------------------------- | --------------------------------------------------------- |
| `tree [id]`                                     | the tree, the whole document by default                    |
| `show <id>`                                     | one element's attributes, and the parent it hangs from     |
| `history [n]`                                   | edits around HEAD, newest first; 20 by default |
| `canvas` / `canvas <w> [h]`                     | read the document's size / set it, and start the history over |
| `create rect <id> <x> <y> <w> <h> [fill]`       | a rectangle                                                |
| `create circle <id> <cx> <cy> <r> [fill]`       | a circle                                                   |
| `create ellipse <id> <cx> <cy> <rx> <ry> [fill]` | an ellipse                                                |
| `create line <id> <x1> <y1> <x2> <y2> [stroke]` | a line                                                     |
| `create polygon <id> "<x,y ...>" [fill]`        | a closed polygon from at least three coordinate pairs      |
| `create text <id> <x> <y> "content"`            | a text element, and the character data inside it           |
| `create g <id>`                                 | a group                                                    |
| `create box <id> <x> <y> <w> <h> "label" [fill]` | a labelled box: a marked group with a rectangle and text |
| `delete <id>`                                   | the node and its descendants                               |
| `move <id> <dx> <dy> [transform\|params] [cascade\|restrict]` | a node's params or transform, or a container's descendants |
| `scale <id> <factor> [transform\|params] [cascade\|restrict]` | a node's params or transform, or a container's descendants |
| `set <id> <attr> <value>`                       | one editable attribute; `id`, parent and children are not attributes |
| `group <newGid> <id> <id> ...`                  | a new group, taking those nodes' place                     |
| `ungroup <gid>`                                 | the group's children, back in its place; top-level groups are refused |
| `addto <gid> <id>`                              | the node, as the last child (the front) of a container     |
| `front <id>` / `back <id>`                      | the ends of the child order, the last child in front       |
| `undo` / `redo` / `checkout <commit> [n]`       | one instruction back, forward, or to a commit shown by `history`; checkout also prints its timeline |
| `init [w] [h]`                                  | a fresh document, 800x600 by default                       |
| `read <file.svg>`                               | read a document                                            |
| `save [file.svg]`                               | write it, where it was read from by default                |
| `help` / `exit`                                 | show the command list / leave the editor                   |

Every `create` also takes `in <gid>`, which is where the element goes: without it, the
element is the last child of the root. A node is named by its SVG id; `/` or `svg` names
the root. `svg` is reserved for the root: `create` and `group` refuse it as an id, and
`read` renames a non-root element that carries it, printing a warning with the new id.
The hive's internal handle is not part of the command language.

A box is one instruction and one history entry even though it writes three elements:
`status-rect` and `status-label` are derived children of `g#status`. The group is saved
with `data-kind="box"`, so reading the file restores the same editable box. `tree` and
`show` print the marker as `g#status (box)`. Setting the box's fill updates its outline,
and setting its `text` updates its label, in the same commit. A marked composite cannot
be ungrouped: `ungroup status` is refused as `cannot ungroup composite shape: status`.
A collision with either derived id is refused before the box writes anything.

Polygon points are coordinate pairs separated by spaces or commas, and the list is quoted
as one command word: `create polygon tri "0,0 10,0 5,10" red`. Points use the same length
syntax as other geometry, so units are accepted per coordinate. `set tri points "..."`
replaces the list. `move` and `scale` change every point in the polygon's params.

Quotes group words that contain spaces, which is how text content and file paths are
written. A quote or backslash inside one is written `\"` or `\\`, and the same escaping
is used when the front end prints a value, so the displayed field can be pasted back
unchanged. Quotes do not make an SVG id valid: an id is an XML name, so
`create g "a b"` is refused.

`transform` applies the move or scale to the node's transform matrix. Every SVG node has
that field, so it works for a shape, a text, a group and the root alike. `params` changes values
directly. `restrict` limits the edit to the node itself: on a leaf that is its params, and
on a container it is the container's own transform, so the message is
`g#node2 moved by (0, 100)`. `cascade` changes every descendant's params. A non-container
has no container descendants, so its cascade changes that node's own params. With no
selector, `move` and `scale` use `params` and `cascade`. A node that already carries a
transform is a coordinate-system boundary too: a parent-space offset written into its
params would be read in its local units, so the operation pre-multiplies its matrix
instead. This applies to the node the instruction
named as well as to a transformed descendant; an untransformed node still changes its
params by default. When a cascade reaches such a descendant, it pre-multiplies the move
or scale into that descendant's matrix and leaves the subtree below alone, because the
new matrix already covers it.

A `move` answer uses one sentence for every mode: `<target> moved by (x, y)`. A cascade
adds how many inner elements changed: `g#g1 moved by (5, 7) (3 elements)`. The
mode is still in `event::Moved`, but is not repeated in the prose.

Other edit answers keep the subject first as well: `g#gg created (2 elements)`,
`g#gg ungrouped into g#layer (2 elements)`, and `circle#c1 scaled by 2`. A text edit
reports its value directly: `text#t updated: text="Hello World"`.

A transform list is one or more of `translate(x [y])`, `scale(x [y])`,
`rotate(deg [cx cy])`, `skewX(deg)`, `skewY(deg)` and `matrix(a b c d e f)`.
The list is composed into one matrix when it is read. `show` and a saved file print that
matrix in TRS+skew form - `translate(...) rotate(...) scale(...) skewX(...)` or
`skewY(...)` - leaving out identity steps. Both skew decompositions are exact, so the
shorter rendering is printed; a tie prefers the smaller absolute skew angle. A plain
`skewX(15)` or `skewY(15)` therefore reads back unchanged, while `rotate(30 10 20)` reads
back as `translate(11.339746 -2.320507) rotate(30)`.

Moving or scaling an empty container is refused with
`error: cannot move or scale an empty group: g#node2`; a loaded-only container kind
names its element in the same sentence. Such an edit would be
invisible, but a shape inserted into that container later would inherit the displaced
coordinate system.

`init`, `read`, `save`, and `canvas` establish an IO baseline. At that baseline `undo`
says `error: undo not allowed after io command`; once an edit has been made, undo works
until the baseline is reached again. Undo and redo report the edit outcome they move:
`undo: rect#box moved by (5, 7) (HEAD 2)` and
`redo: rect#box moved by (5, 7) (HEAD 3)`.

`history [n]` shows newest commits first and the baseline last. `n` counts neighboring
checkpoints, split between the future above HEAD and the past below it; when one side
reaches an end, the other side grows. Baseline, HEAD, and the newest commit remain visible
as anchors even when they fall outside that window. A line such as
`... 7 commits folded ...` reports an omitted range between visible checkpoints, and is
omitted when that range is empty.
`init`, `read`, `save`, and `canvas` establish a new baseline and therefore cut the
editable part of the list back to nothing.
`checkout <commit> [n]` moves straight to that commit, keeping the rest of the branch
available for redo, then prints the retained timeline with the new HEAD marked. The
timeline uses the same default cap of 20 neighboring commits as `history`; `n` overrides
it. Checking out the commit that is already HEAD answers
`checked out <commit> (no effect)` before the timeline.

Checkpoints reuse the checkout id they preserve, while `init`, `canvas`, and a `read`
chunk create real commits. A read creates its root in the first load commit rather than
creating and then replacing a throwaway root, so a one-chunk read advances the visible
sequence by one.

`show` prints the node's parent on its heading when it is not the root, then one
indented line for each group of attributes:

```text
svg> show c1
circle#c1  parent=g#markers
  cx=100 cy=100 r=20
  fill=red
```

Two lengths only add up inside one system: mm and cm are metric, in, pt and px are
defined against the inch, and the two are never mixed - not even where the arithmetic
is exact, because a document holding `1mm + 1in = 26.4mm` no longer says that two
systems were written. The refusal names the two sides, so `1in` against `1cm` says
which of them is imperial.

Canvas sides must be positive. Shape dimensions and radii may be zero, but not
negative. Display text uses up to six decimal places, falls back to the exact spelling
when rounding would show a present value as zero, and suppresses negative zero.
Files use the shortest spelling that reads back as the same `float`, so a sub-micro
geometry value is not rounded away and float32 noise does not become a long decimal.

`read` parses the SVG subset the model represents and assigns ids to unnamed
non-root elements, printing one `warning:` line for each generated id. `save` writes
that model tree back as deterministic UTF-8 SVG. Elements or attributes outside the
subset are refused rather than silently dropped; [Model.md](Model.md#svg-file-io) lists
the accepted syntax. The root keeps its `xmlns` and `viewBox`; text elements keep
`text-anchor` and `font-size`. Both commands report the number of non-root SVG elements:
`read diagram.svg (12 elements)` and `saved to diagram.svg (12 elements)`.

## Modules and dependency rules

| target              | directory           | links                              |
| ------------------- | ------------------- | ---------------------------------- |
| `SvgEditor::Model`  | `Model/`            | MVCCHive                           |
| `SvgEditor::FileSystem` | `FileSystem/`   | Model, Protocol, tinyxml2          |
| `SvgEditor::Protocol`   | `Protocol/`     | Model (for the values)             |
| `SvgEditor::Editor` | `Editor/`           | Model, Protocol                    |
| `SvgEditor::Planner`| `Planner/`          | Model, Editor, FileSystem, Protocol |
| `SvgEditor::Ui`     | `Ui/`               | Protocol, Model                    |
| `Cli`               | `Ui/`               | Ui, Planner, Model, FileSystem     |

`Ui/` holds the text front end: the instruction syntax, the value syntax, and a
library that a test can link. `Cli` is the small executable around it and the composition
root: it owns the document, constructs Editor and FileSystem, and gives all three to the
Planner. What it must not do is decide what an instruction means.

Only `Model/` may *use* MVCCHive: its transactions, commit ids and virtual ids are
Model's business, and an edit or a read from any other module goes through Model's
own API. `Document` holds the arena by value, so the hive's headers are on every
module's include path and the link line cannot express that rule - the `layering`
test reads the sources instead and fails if a module outside `Model/` names it.
