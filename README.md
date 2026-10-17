# SVG Editor

An SVG editor built as a small pipeline: the text front end parses an instruction
into a typed intent, the Planner decides what it means, and either Model (a view, or
a file) or the Editor (a tree-level transaction) serves it. `Structure.md` is the
design document, and the module table at its top is the map of the build.

## Requirements

- A C++23 toolchain. That is the language this project is written in;
  [MVCCHive](https://github.com/NegaVoayz/MVCCHive) is written in C++23 too, so
  the one dependency asks nothing extra of the toolchain.
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

The first configure fetches MVCCHive at the tag pinned in
`Model/cmake/MVCCHive.cmake`. A package config found by `find_package` or a
checkout pointed at by `FETCHCONTENT_SOURCE_DIR_MVCCHIVE` avoids the download:

```sh
# uses ../MVCCHive as it is, no network involved
cmake --preset local-dep
cmake --build --preset local-dep
ctest --preset local-dep
```

`release` is the same as `default` with `CMAKE_BUILD_TYPE=Release`. To move the
dependency instead of taking it from next door:

```sh
cmake --preset default \
  -DSVG_EDITOR_MVCCHIVE_REPOSITORY=https://host/mirror/MVCCHive.git \
  -DSVG_EDITOR_MVCCHIVE_TAG=v1.0.0
```

## Running

```sh
build/ninja-local-dep/Ui/svg-editor.exe     # the prompt, one instruction per line
build/ninja-local-dep/Ui/svg-editor.exe "create g markers; create rect box 0 0 10 10 #ff0000 in markers; tree"
```

The prompt is `svg>`, `help` lists the commands, and one line may hold several
instructions separated by `;`. A view is printed as what it describes, and anything
else as the sentence that says what happened:

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

The instructions are:

| what                                            | what it does                                              |
| ----------------------------------------------- | --------------------------------------------------------- |
| `tree [id]`                                     | the tree, the whole document by default                    |
| `show <id>`                                     | one element's own attributes                               |
| `canvas` / `canvas <w> [h]`                     | read the document's size / set it, and start the history over |
| `create rect <id> <x> <y> <w> <h> [fill]`       | a rectangle                                                |
| `create circle <id> <cx> <cy> <r> [fill]`       | a circle                                                   |
| `create ellipse <id> <cx> <cy> <rx> <ry> [fill]` | an ellipse                                                |
| `create line <id> <x1> <y1> <x2> <y2> [stroke]` | a line                                                     |
| `create text <id> <x> <y> "content"`            | a text element, and the character data inside it           |
| `create g <id>`                                 | a group                                                    |
| `delete <id>`                                   | the node and everything under it                           |
| `move <id> <dx> <dy>`                           | a shape's coordinates, or a container's transform          |
| `scale <id> <factor>`                           | a shape's values, or a container's transform               |
| `set <id> <attr> <value>`                       | one editable attribute; `id`, parent and children are not attributes |
| `group <newGid> <id> <id> ...`                  | a new group, taking those nodes' place                     |
| `ungroup <gid>`                                 | the group's children, back in its place                    |
| `addto <gid> <id>`                              | the node, as the last child of a container                 |
| `front <id>` / `back <id>`                      | the ends of the child order, the first child in front      |
| `undo` / `redo`                                 | one instruction back or forward                            |
| `init [w] [h]`                                  | a fresh document, 800x600 by default                       |
| `read <file.svg>`                               | read a document                                            |
| `save [file.svg]`                               | write it, where it was read from by default                |

Every `create` also takes `in <gid>`, which is where the element goes: without it, the
element is the last child of the root. A node is named by its SVG id, by `#<handle>`,
or by `/` for the root.

Two lengths only add up inside one system: mm and cm are metric, in, pt and px are
defined against the inch, and the two are never mixed - not even where the arithmetic
is exact, because a document holding `1mm + 1in = 26.4mm` no longer says that two
systems were written. The refusal names the two sides, so `1in` against `1cm` says
which of them is imperial.

`read` and `save` are wired to the file layer already and report that the SVG parser
and serializer behind them are not written yet; everything above them is in place.

## Modules and dependency rules

| target              | directory           | links                              |
| ------------------- | ------------------- | ---------------------------------- |
| `SvgEditor::Model`  | `Model/`            | MVCCHive                           |
| `SvgEditor::FileSystem` | `Model/FileSystem/` | -                              |
| `SvgEditor::Protocol`   | `Protocol/`     | Model (for the values)             |
| `SvgEditor::Editor` | `Editor/`           | Model, Protocol                    |
| `SvgEditor::Planner`| `Planner/`          | Model, Editor, Protocol            |
| `SvgEditor::Ui`     | `Ui/`               | Protocol, Model                    |
| `Cli`               | `Ui/`               | Ui, Planner, Model                 |

`Ui/` holds the text front end: the instruction syntax, the value syntax, and a
library that a test can link. `Cli` is the small executable around it, and it links
Model because it is the composition root - it owns the document and hands it to the
Editor and the Planner. What it must not do is decide what an instruction means:
that is the Planner's job, and nothing else in the pipeline routes.

Only `Model/` may *use* MVCCHive: its transactions, commit ids and virtual ids are
Model's business, and an edit or a read from any other module goes through Model's
own API. `Document` holds the arena by value, so the hive's headers are on every
module's include path and the link line cannot express that rule - the `layering`
test reads the sources instead and fails if a module outside `Model/` names it.
