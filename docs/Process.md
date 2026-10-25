# Development Process Record

This record describes how the SVG editor was specified, changed and checked. It is part of
the submission, not a tutorial: [README.md](README.md) explains how to use the program,
[QualityAssurance.md](QualityAssurance.md) records the verification strategy, and
[Consistency.md](Consistency.md) describes the automated self-check. The
[AI-assisted iteration review](Process.md#ai-assisted-iteration-review) records
representative problems found in AI-produced advice, documents and code and the evidence
used to correct them. [Submission.md](Submission.md) records how the tagged archive is
retargeted, refreshed and built.

## Scope and baseline

The submission baseline for the A-08 through A-13 issue round was commit `6396838`.
Before making changes, every reported defect was reproduced against the built CLI, and
every design claim was traced to its implementation or test. The resulting work kept the
existing architecture:

- `Ui` owns text syntax and user-facing wording.
- `Planner` routes typed intents and owns display-only session state.
- `Editor` turns one change intent into one Model transaction.
- `FileSystem` owns SVG/XML parsing, serialization, paths and context baselines.
- `Model` owns the tree, history, and storage.
- `Protocol` contains the typed vocabulary shared by those modules.

## Change method

Each issue followed the same loop:

1. Reproduce the behavior with the CLI or a focused unit test.
2. Identify the layer that owns the invariant, rather than patching only the visible
   command.
3. Represent invalid states with explicit types and compile-time dispatch.
4. Add a regression test at the narrowest useful level and an end-to-end CLI test when
   output or history behavior was involved.
5. Build with warnings as errors and run the full test suite.
6. Commit one coherent change with the `Type: concise description` message convention.

This kept fixes reviewable and made regressions attributable to a single decision.

## AI-assisted iteration review

The following episodes are representative of the review loop. Each row records the faulty
artefact, the evidence that exposed it and the durable correction; commit hashes identify
the exact changes, while the quoted commands and output make the discovery reviewable.

| proposal or artefact | what was wrong | how it was confirmed | how it was corrected | how it is prevented from recurring |
| --- | --- | --- | --- | --- |
| The storage chapter claimed that a quoted run could not contain a quote. | Quote and backslash escaping were already implemented and tested. | `build/ninja-local-dep/Ui/svg-editor.exe 'init; create text t 0 0 "a\"b"; show t'` printed `text="a\"b"`. | The obsolete claim was removed in `0f8a329`. | The parser and rendering tests retain escaped-input coverage, and `consistency` executes the quoted-text transcript. |
| An AI-proposed refactor put `element_name` over a nested `of()` table. | A comment claimed a missing element mapping was a compile error, but ordinary overload resolution could report only that no implementation existed; the detail template was not the public failure point. | <code>git show --format= --unified=0 d754d9e -- Model/include/ElementName.hh</code> showed <code>-constexpr ... of()</code> and <code>-namespace element_name_detail</code> replaced by <code>+constexpr ... element_name(const T&amp;)</code> with <code>static_assert(always_false&lt;T&gt;, ...)</code>. | `d754d9e` made the public primary template the single checked mapping and removed the extra detail layer. | Adding an alternative without an element specialization now fails in that primary template, rather than producing an unrelated overload error. |
| An AI issue report said the three-point polygon rule held at every entry point. | It held for `create`, but both the UI `set points` parser and the FileSystem SVG reader still accepted fewer than three points. | <code>git show --format= --unified=0 35f88af -- Ui/src/SvgValue.cc FileSystem/src/SvgValueReader.cc</code> showed both <code>-if (values.size() % 2 != 0)</code> checks becoming <code>+if (values.size() &lt; 6 &#124;&#124; values.size() % 2 != 0)</code>. | The invariant moved into both value parsers in `35f88af`, with `set` and `read` refusal tests. | `consistency` executes both refusal paths, and parser-level tests reject short lists before an edit transaction or file replacement. |
| AI-produced history outcomes used nullable shared ownership for values copied from the replay log. | A copied undo/redo outcome needed one owner for one event, not shared storage; `std::shared_ptr` obscured that lifetime and left stale snapshot handling around live history. | <code>git grep -n shared_ptr ea0ae42^ -- Protocol Editor</code> reported the pointers in both the history implementation and undo/redo events; <code>git show --format= --unified=0 ea0ae42 -- Protocol/include/Event.hh</code> showed both fields changing to <code>std::optional&lt;EditEvent&gt;</code>. | `f49e009` made history views read the live replay log and cursor, and `ea0ae42` stored the selected edit outcome by value. | History entries retain values, the view refers to the current deque and its parameters, and rendering resolves an entry only when the view is used. |
| The self-check reported `CLI transcript 'polygon' does not match`. | The CLI binary could not start because its runtime directory was absent from `PATH`; the transcript was never produced. | <code>cmake "-DSOURCE_DIR=$PWD" "-DBINARY_DIR=$PWD/build/ninja-local-dep" -DCTEST_COMMAND=ctest -DCLI=missing-cli -P cmake/CheckDocumentation.cmake</code> prints <code>cannot run CLI transcript 'polygon' (status no such file or directory)</code>, while running the generated command by hand succeeds. | `13ad4ad` classified launch failures separately from transcript mismatches. | The checker validates launch status before comparing output while preserving intentional nonzero command results. |
| `docs/Consistency.md` told readers to use `build/ninja-clang-local-dep`. | No committed preset created that build directory, so the self-check's own command was not reproducible from the repository. | <code>git grep -n ninja-clang-local-dep 13ad4ad -- docs/Consistency.md</code> printed the old command; <code>CMakePresets.json</code> defines only <code>default</code>, <code>release</code> and <code>local-dep</code>. The replacement <code>ctest --preset local-dep -R '^consistency$' --output-on-failure</code> passes. | `b399ccd` changed the instruction to the committed preset. | The documented command names a preset rather than duplicating a local build directory. |

## A-08 through A-13 record

| issue | decision and evidence |
| --- | --- |
| A-08 | Moved the three-point minimum into both points value parsers. Creation, `set points`, and SVG loading now reject one- and two-point polygons; the Editor keeps `InvalidPoints` as its structured guard. |
| A-09 | Removed the stale claim that quoted text could not contain a quote. Escaped quotes and backslashes are already implemented and tested. |
| A-10 | Added bare coordinate operands to `create polygon` while preserving the quoted-list form. A trailing fill is recognized only after a complete pair sequence. |
| A-11 | Added `zoom` as explicit Planner-owned view state. A positive factor returns `ZoomView`, requires no document, and creates no history commit. |
| A-12 | Distinguished a marked box with a missing derived label from a node that does not support text. The refusal now names the malformed composite and its missing `label` child. |
| A-13 | Added this process record, a quality-assurance chapter, a consistency chapter, and an automated documentation check registered with CTest. |

The earlier issues A-01 through A-07 remain closed by the instruction guard, empty-stage
refusal, shared field descriptors, composite ungroup guard, composite text routing, and
composite marker rendering. Their exact behavior continues to be covered by
`transaction_atomicity`, `pipeline`, `svg_io`, and CLI tests.

## Design decisions

**Parsing belongs to the boundary.** User-command text becomes typed values in `Ui`; file
syntax becomes a staged tree in `FileSystem`. Lower layers never parse command strings.

**Invalid input fails before mutation.** Polygon count checks, opacity range checks,
invalid SVG reads, and missing composite children all complete before a transaction or
document replacement can partially apply.

**View state is not document state.** `zoom` demonstrates the separation directly: it
changes the Planner's display factor but leaves the Document and Editor untouched.

**Compile-time closure is preferred over fallback behavior.** Variants, exhaustive
visitors, and `static_assert` sites make a missing route, payload, attribute spelling, or
renderer case a build error.

**Tests own executable promises.** User-facing examples are paired with parser, rendering,
or CLI tests. The consistency check adds a second pass over documentation links, source
references, recorded metrics, and selected transcript excerpts.

## Process limits

This is a single-machine, single-writer project. There is no issue tracker integration,
reviewer assignment, or CI service record. The repository itself is the process record:
commits are small, build and test commands are reproducible, and the CTest suite is the
acceptance gate.
