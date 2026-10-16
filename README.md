# SVG Editor

An SVG editor built as a small pipeline: the text front end parses an instruction
into a typed intent, the Planner decides what it means, and either Model (a view, or
a file) or the Editor (a tree-level transaction) serves it. `Structure.md` is the
design document, and the module table at its top is the map of the build.

## Requirements

- A C++23 toolchain. That is the language this project is written in;
  [SwapDiffHive](https://github.com/NegaVoayz/SwapDiffHive) is written in C++23
  too, so the one dependency asks nothing extra of the toolchain.
- CMake 3.24 or newer. The dependency needs 3.20 on its own; the extra is for what
  this build does with it, `FIND_PACKAGE_ARGS` in the fetch.

## Building

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The first configure fetches SwapDiffHive at the tag pinned in
`Model/cmake/SwapDiffHive.cmake`. Two things avoid the download: an installed
SwapDiffHive (found with `find_package`), or a checkout pointed at by
`FETCHCONTENT_SOURCE_DIR_SWAPDIFFHIVE`:

```sh
# uses ../SwapDiffHive as it is, no network involved
cmake --preset local-dep
cmake --build --preset local-dep
ctest --preset local-dep
```

`release` is the same as `default` with `CMAKE_BUILD_TYPE=Release`. To move the
dependency instead of taking it from next door:

```sh
cmake --preset default \
  -DSVG_EDITOR_SWAPDIFFHIVE_REPOSITORY=https://host/mirror/SwapDiffHive.git \
  -DSVG_EDITOR_SWAPDIFFHIVE_TAG=v1.1.0
```

## Running

```sh
build/ninja-debug/Ui/svg-editor.exe "create g layer; create rect box 0 0 10 10 #ff0000; addto layer box; move box 5 5; tree"
build/ninja-debug/Ui/svg-editor.exe     # reads instructions from stdin
```

```
[change] created #1 <g> id="layer" under the root
[change] created #2 <rect> id="box" under the root
[change] moved #2 to the end of #1
[change] moved #2 by 5 5
[view] #0 <svg>
  #1 <g> id="layer"
    #2 <rect> id="box" x="5" y="5" w="10" h="10" fill="#ff0000"
```

The instructions are `tree`, `find <svg-id>`,
`create rect <id> <x> <y> <w> <h> [fill]`, `create g <id>`, `delete <id>`,
`move <id> <dx> <dy>`, `addto <gid> <id>`, `undo`, `redo`, `open <path>` and
`save <path>`. `move` translates a shape's own coordinates and adds a transform to
a container. Two lengths only add up inside one system: mm and cm are metric, in, pt
and px are defined against the inch, and the two are never mixed.

## Modules and dependency rules

| target              | directory           | links                              |
| ------------------- | ------------------- | ---------------------------------- |
| `SvgEditor::Model`  | `Model/`            | SwapDiffHive                       |
| `SvgEditor::FileSystem` | `Model/FileSystem/` | -                              |
| `SvgEditor::Protocol`   | `Protocol/`     | -                                  |
| `SvgEditor::Editor` | `Editor/`           | Model, Protocol                    |
| `SvgEditor::Planner`| `Planner/`          | Model, Editor, Protocol            |
| `SvgEditor::Ui`     | `Ui/`               | Protocol, Model                    |
| `Cli`               | `Ui/`               | Ui, Planner, Model                 |

`Ui/` holds the text front end: the instruction syntax, the value syntax, and a
library that a test can link. `Cli` is the small executable around it, and it links
Model because it is the composition root - it owns the document and hands it to the
Editor and the Planner. What it must not do is decide what an instruction means:
that is the Planner's job, and nothing else in the pipeline routes.

Only `Model/` may *use* SwapDiffHive: its transactions, its diff ids and its slots
are Model's business, and an edit or a read from any other module goes through
Model's own API. `Document` holds the arena by value, so the hive's headers are on
every module's include path and the link line cannot express that rule - the
`layering` test reads the sources instead and fails if a module outside `Model/`
names it.
