# Quality Assurance

Quality is enforced at four levels: compilation, focused tests, end-to-end pipeline tests,
and documentation checks. A change is complete only when all four agree.

## Test layers

| layer | what it proves | representative targets |
| --- | --- | --- |
| Compile-time structure | every closed variant has a route or renderer; missing payload, attribute, or intent cases fail the build with `-Werror` or `/WX` | `static_assert` sites in Protocol, Model, Editor, FileSystem and Ui |
| Unit tests | value parsing, geometry, transforms, tree storage, history storage, rendering and SVG round trips | `document`, `geometry`, `transform_matrix`, `rendering`, `svg_io`, `file_system` |
| Integration tests | typed intentions reach the owning module and preserve atomicity | `pipeline`, `create_atomicity`, `transaction_atomicity` |
| CLI tests | documented command chains, exact structured output, refusals, files and exit behavior | the `cli_*` registrations in CTest |
| Architecture checks | MVCCHive stays in Model and tinyxml2 stays in FileSystem | `layering` |
| Documentation checks | links, paths, SHAs, recorded metrics and selected command transcripts stay current | `consistency` |

The test suite includes positive and negative paths. Examples include invalid polygon
point counts, opacity outside `[0,1]`, malformed marked boxes, failed SVG replacement,
empty transactions, duplicate ids, invalid units, transformed-container movement, history
checkout, and save/read round trips.

## Measured snapshot

| metric | recorded value |
| --- | ---: |
| Source commits at the validated snapshot | 172 |
| CTest registrations | 128 |
| static_assert occurrences | 67 |
| source files containing static_assert | 21 |

The `consistency` test recomputes these values. A mismatch fails the suite so the recorded
facts cannot silently drift.

## Required local verification

Builds use at most 16 parallel jobs. The local dependency preset avoids network access;
the default and Release presets verify the normal build paths.

```sh
cmake --preset local-dep
cmake --build --preset local-dep -- -j 16
ctest --preset local-dep --output-on-failure -j 16
```

```sh
cmake --preset default
cmake --build --preset default -- -j 16
ctest --preset default --output-on-failure -j 16
```

```sh
cmake --preset release
cmake --build --preset release -- -j 16
ctest --preset release --output-on-failure -j 16
```

The Clang and GCC local-dependency builds are both used during review. Clang is the
current primary compiler for warning diagnostics; GCC guards the MinGW toolchain used by
the project.

## Acceptance rules

- Do not commit with compiler warnings; warnings are errors for project sources.
- Do not accept a new user-facing command without parser, rendering and CLI coverage.
- Do not accept a tree edit without an undo/redo atomicity check for the affected verb.
- Do not accept an SVG subset change without reader and writer round-trip coverage.
- Do not weaken exhaustive dispatch or compile-time assertions to make a new case build.
- Run `git diff --check` before each commit.
- Run the full CTest suite after the final change in a round.

## Manual review

Automated tests cannot prove that prose is persuasive or that an architecture choice is
appropriate. Review therefore also checks:

- the dependency direction in [Structure.md](Structure.md);
- vocabulary and command routing in [Pipeline.md](Pipeline.md);
- storage and cost claims in [Model.md](Model.md);
- development history and decisions in [Process.md](Process.md);
- the self-check coverage and limits in [Consistency.md](Consistency.md);
- the moving-tag refresh and archive checks in [Submission.md](Submission.md).
