# Development Process Record

This record describes how the SVG editor was specified, changed and checked. It is part of
the submission, not a tutorial: [README.md](README.md) explains how to use the program,
[QualityAssurance.md](QualityAssurance.md) records the verification strategy, and
[Consistency.md](Consistency.md) describes the automated self-check.

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
