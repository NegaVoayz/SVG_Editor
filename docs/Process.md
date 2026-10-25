# Development Process Record

This record describes how the SVG editor was specified, changed and checked. It is part of
the submission, not a tutorial: [README.md](README.md) explains how to use the program,
[QualityAssurance.md](QualityAssurance.md) records the verification strategy, and
[Consistency.md](Consistency.md) describes the automated self-check. The
[project construction log](Process.md#project-construction-log) traces the architecture
from the first module map through the released structure. The
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

## Project construction log

This log groups the commit history into architecture-level phases. It is deliberately not
a list of every feature: each row records what shape the system had, what structural work
moved it forward, and which problem or violated boundary forced the next correction. It
also distinguishes user-directed redesigns from changes initiated by the implementation
work, because both affected the architecture and the second kind sometimes produced
layers that had to be removed later. The commit sequence can be reconstructed with:

```sh
git log --reverse --date=short --format='%h %ad %s'
```

| period | structure and work | problem or design correction | key commits |
| --- | --- | --- | --- |
| 2026-10-16: designed skeleton | The project began as a module map and empty CMake targets. The initial design had a typed `Ui -> Planner -> Editor -> Model -> SwapDiffHive` pipeline, an RAII transaction around every user edit, Protocol values shared across the boundary, and the text front end as composition root. Model alone could use the hive. | The design claims had no executable counterpart yet. The first vertical slice and pipeline test made the ownership and routing rules testable instead of aspirational. | `f6e121f`, `d744116`, `8b60141`, `1857581`, `91418f5`, `602eae6`, `ba7b012` |
| 2026-10-16: first vertical feature path | Model gained a tree, lengths, canvas resets, sibling order and shape vocabulary. Views became data rather than pre-rendered strings, so UI alone owned wording. The Editor vocabulary expanded to create, move, scale, group, ungroup and reorder operations. | The first files mixed several decisions as features accumulated. Handlers, protocol types and parsing responsibilities needed smaller structural boundaries before more commands were added. | `d24a646`, `0ebc161`, `05becb9`, `8648fec`, `282b23b`, `b16a985`, `2ce964f` |
| 2026-10-16 to 2026-10-17: modular decomposition | The Editor was split by the decision being made: create, history, payload and structure. Protocol was split into intents, node references, outcomes and views. The UI parser was split into command, create, syntax and value files; verb readers became table rows, and create became one intent with six element alternatives. | Per-command `if` chains and declaration-only helper layers made additions easy to route inconsistently. Explicit tables and primary-template `static_assert` failures replaced duplication, while the help test made the verb table observable. | `92bb10a`, `bb05270`, `9bbbcfb`, `d5b837c`, `7017912`, `e6b5abc`, `4612f2e`, `bd7608d` |
| 2026-10-18: user-directed storage-engine pivot | On the user's direction, the tree moved from four `SwapDiffHive` arenas - nodes, parent links, child links and sibling edges - to one MVCCHive of `Node` records that carry their own tree links. Diff ids became MVCCHive virtual ids, and history moved to exact hive checkouts. | The four-way layout had split one logical edit into several small records and parallel histories. It minimized individual rows while increasing write amplification, coordination, and the number of places that had to agree about one edit. One record store and one checkout removed that cost and left a substantially smaller, more direct model: the node and its place in the tree travel together, and one commit describes one edit. | `0a2b289`, `073f25a`, `ded772b`, `2e8b9e0`, `ad3b588`, `ae5d53a`, `55162dd` |
| 2026-10-18: protocol semantics and temporary I/O debt | The protocol gained structured outcomes and refusals, set-attribute intents and touched-node results for undo/redo. A tinyxml2 adapter, SVG reader, writer and assigned-id reporting were added under `Model/FileSystem`, so file I/O could grow behind a seam. | Model still owned the tree, and the temporary placement gave Model knowledge of XML, file paths and a parser dependency. This was usable feature growth but violated the intended top-level I/O role and became explicit architectural debt. | `eed2a6e`, `d2449ef`, `a71ca99`, `f1a3ef1`, `2fb35bf`, `60ac415`, `42695c6`, `50945e9`, `dd36a1b` |
| 2026-10-19: coordinate systems and history semantics | Transform lists became linked semantic operations that materialize as matrices. The Editor distinguished `params`, `transform`, `cascade` and `restrict`, stopped cascades at transformed descendants, and added rotate-center and skew support with stable decomposition and formatting. | Early moves mixed local and parent spaces, transformed containers behaved differently from leaves, and empty or restricted container edits could be invisible. The corrected rule is that a coordinate-system boundary receives the operation in its own matrix. | `dcb5ef1`, `7968156`, `a489d01`, `01e03bd`, `2c2d7cd`, `5ac82f2`, `2eeb662`, `20642d7`, `878851f` |
| 2026-10-19 to 2026-10-22: replayable history and one output model | History gained replayable edit outcomes, one explicitly assigned MVCCHive commit id, checkout, baseline windows and capped timelines. Node fields, event targets, tree/show/save order and user-facing sentences were unified around Protocol data. | The UI and Model had drifted in field order and message forms, while undo/redo could lose the event that described a commit. The first replay implementation then added a second `GlobalCommitId` and a global-to-local hive mapping; the user exposed that compensating layer by asking whether MVCCHive itself accepted an explicit commit id. `d56184e` removed the second identity and used the hive's native commit id for every participating hive. | `9b7236a`, `90b828c`, `d56184e`, `d6beeff`, `f1eedb6`, `37970cc`, `435284d`, `adf7b3b`, `cface72` |
| 2026-10-22: compile-time and ownership audit | Element naming became an explicit type-to-name template; exhaustive dispatch was enforced across visitors; source audit fixes removed nullable or accidental paths. Floating and transform formatting gained exact read-back behavior. | An earlier nested `of()` table looked checked but failed through overload resolution instead of the project's dependent `static_assert`. The public primary template became the single failure point, and audit rules were written into the structure. | `0cd4efb`, `d754d9e`, `95db9b5`, `87c1424` |
| 2026-10-23: composites, schema and the I/O boundary repair | Atomic box creation introduced a composite that writes several nodes in one transaction. SVG kind registration and composite edits were made exhaustive. A reflection-cpp schema attempt was replaced by shared, compile-checked field identifiers; line rendering moved toward FileSystem. Staged tree replacement then let FileSystem become a top-level peer of Editor, with Planner routing typed file intents. | The accepted Model-owned I/O debt became the main violation to repair. The final graph has one write owner for tree edits and another for context/file operations, while both reach Model through explicit APIs. | `e32b68c`, `585da16`, `cb406ef`, `31ef2d1`, `89ba608`, `94dbc1e`, `299451a`, `dd132b5`, `c6013ab` |
| 2026-10-24: value ownership and live views | Nullable schema pointers became optional values, `get_if` inspections became alternative checks, history views became references to the Editor's live deque and cursor, and undo/redo outcomes became `std::optional` values. Absent lengths became explicit, and stored event names were reduced to their required size. | Shared pointers and copied history snapshots obscured ownership and could show stale history after an edit. The corrected model is value ownership for retained entries, references for live views, and explicit alternatives instead of nullable type inspection. | `829c31e`, `137c3f7`, `f49e009`, `ea0ae42`, `4595db4`, `c8b13fb` |
| 2026-10-25: completed feature set and release engineering | Polygons, opacity, bare polygon coordinates, non-committing zoom and precise malformed-box diagnostics completed the implemented SVG subset. MinGW large-object support made the growing translation units build. Process, quality, consistency and submission chapters introduced executable delivery checks. | The self-check itself exposed stale untracked notes, a misclassified CLI launch failure and a non-reproducible build path. Those were repaired, and A-17 and A-18 added the broader construction record and the moving-tag archive policy. | `1de962e`, `dadf0b2`, `99d3aea`, `4e36e4c`, `0ce542d`, `80ee9d2`, `6b673dd`, `f370c29`, `13ad4ad`, `b399ccd`, `bb18563`, `06313dc` |

The structural trajectory can be read as two graphs:

```text
initial:  Ui/Cli -> Planner -> Editor -> Model -> SwapDiffHive
                                \-> Model/FileSystem (I/O seam)

current:  Cli -> Planner -> Editor -----> Model -> MVCCHive
                          \-> FileSystem -> Model
                                         -> TinyXml2
```

### User interventions and rejected `A over A` layers

The storage pivot was a user-triggered architectural decision, not an implementation-side
discovery. The earlier design could be patched indefinitely, but the user rejected the
premise of maintaining four linked stores for one tree and directed the project toward one
MVCCHive. The result addressed a real performance problem - one edit no longer had to
produce and align several histories - while also producing the cleaner model described
above. That intervention is part of the design history because later work built on it.

An `A over A` layer is parallel state or a renamed API added above an existing abstraction
to compensate for something it does not expose directly. It looks like a local fix, but
it creates two representations that must agree. The project repeatedly accepted such a
layer, found the second source of truth, and then removed it:

| rejected compensating layer | why it was the wrong shape | final correction | commits |
| --- | --- | --- | --- |
| `std::vector<Snapshot> checkpoints_` plus `rewind_head_to_view()` in Model | MVCCHive 1.x did not yet expose the checkout operation Model needed, so Model kept its own history list and rewrote the hive head to the visible snapshot before an edit after undo. This duplicated history state and turned one history operation into ordinary commits. | After adopting MVCCHive's checkout, `TreeStore` kept only the current snapshot and depth/position counters; the checkpoint vector and head-rewind path disappeared. | `073f25a`, `55162dd` |
| A synthetic `Removed` alternative in `Node` | Erasure was represented as a fabricated payload so an older snapshot could still materialize the record. Storage tombstone semantics leaked into the document type, and every node visitor had to know about a value that was not an SVG node. | The hive erase became the representation; snapshots retained old versions, and `Node` returned to only SVG elements and plain text. | `073f25a`, `55162dd` |
| `GlobalCommitId` plus `Checkout { global, hive }` mapping and `hive_commit()` lookup | History replay needed one checkout across several hives, so the implementation introduced a second identifier layer above MVCCHive's `CommitId`. Every commit then had to maintain and validate two identities, and the mapping could disagree with the hive. | The user asked whether MVCCHive could accept a caller-supplied commit id. It could, so one process-wide allocator passes the same native `CommitId` to every participating hive; the extra type, pair and lookup were deleted. | `90b828c`, `d56184e` |

The recurring rule is to inspect the dependency's actual interface before adding a Model
type beside it. If the missing primitive is legitimate, expose it at the dependency
boundary and keep one owner. A parallel snapshot list, tombstone payload or second commit
identifier is only cheap at the point it is introduced; thereafter every edit has to keep
both representations honest.

The largest decisions were cumulative rather than replacements. The closed `Intent`
vocabulary and `variant + visit + handle<T>` dispatch survived every phase. The transaction
boundary became a single MVCCHive edit. The I/O code moved from a nested seam to a peer
module. Views moved from copied snapshots to a live history reference only where the
viewport semantics require it. Each change narrowed one ambiguity: who owns the state, who
may mutate it, and which type makes an invalid route fail before runtime.

## AI-assisted iteration review

The construction log above is the broad project path. This table is a focused evidence
appendix for representative places where advice, prose or generated code disagreed with
the structure. Each row records the faulty artefact, the evidence that exposed it and the
durable correction; commit hashes identify the exact changes, while the quoted commands
and output make the discovery reviewable.

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
