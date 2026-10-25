# Consistency Self-Check

The submission uses a machine-checkable self-check rather than relying only on manual
proofreading. [CheckDocumentation.cmake](../cmake/CheckDocumentation.cmake) runs as the
CTest named `consistency`.

## Recorded facts

The checker parses this table and compares the values with the source tree and generated
test index:

| metric | recorded value |
| --- | ---: |
| Source commits at the validated snapshot | 164 |
| CTest registrations | 128 |
| static_assert occurrences | 67 |
| source files containing static_assert | 21 |

## What is checked

1. Every required submission document exists.
2. Every relative Markdown link points to an existing document.
3. A link fragment such as `Model.md#svg-file-io` names an existing heading.
4. Backticked repository paths with a source or document extension exist.
5. A backticked `path:line` reference, when used, points inside the referenced file.
6. Every hexadecimal source revision named in the documentation resolves to a commit.
7. Recorded commit, test, and compile-time-assertion counts match fresh measurements.
8. Representative documented CLI transcripts are executed against the built CLI and
   matched as substrings. These cover polygons, box reuse, zoom, and malformed-composite
   diagnostics.

## Why this exists

Two defects in this round were consistency failures rather than algorithm failures:

- the polygon three-point rule was documented but enforced at only one of three entry
  points;
- the storage chapter claimed quote escaping was unavailable after it had been added.

The self-check reduces the chance that a rule changes in one layer or a limitation remains
documented after the code moves on. It does not replace semantic review: it verifies
mechanical references and selected transcripts, while engineers still review design
claims, failure modes, and whether tests exercise the right invariant.

## Running only the check

```sh
ctest --test-dir build/ninja-clang-local-dep -R '^consistency$' --output-on-failure
```

When a check fails, fix the documentation or the implementation until they describe the
same system. Updating only the expected number is acceptable when a real metric changed;
updating it to hide a broken link or failed transcript is not.
