# Submission Package

The submission is the annotated Git tag `final`. This repository intentionally treats
`final` as a moving release alias: the tag is retargeted when a tested issue round becomes
the deliverable. Ordinary `git fetch` and `git pull` do not overwrite an existing local
tag, so a clone can retain an older `final` without failing. The archive procedure below
therefore force-refreshes tags and selects the tag explicitly.

## Publishing a new final

Run the acceptance checks in [QualityAssurance.md](QualityAssurance.md) from the commit
that should become the package. Then publish that commit and retarget the annotated tag:

```sh
git push origin master
git tag -f -a final -m final HEAD
git push --force origin refs/tags/final
```

The force update is intentional for this moving alias. The commit hashes and build output
remain in [Process.md](Process.md) and the test record.

## Building the archive

Start with a clean clone or a clean checkout. Refresh tags even when the checkout has not
changed:

```sh
git fetch --tags --force
git switch --detach final
git status --short
git rev-parse 'final^{commit}'
git rev-parse HEAD
```

`git status --short` must print nothing. The two revision commands must print the same
full commit hash; if they do not, stop rather than package an ambiguous tree. Run the
acceptance checks for the checked-out commit, then archive the tag itself so the command
does not depend on local uncommitted files:

```sh
ctest --preset local-dep --output-on-failure -j 16
git archive --format=zip --prefix=SVG_Editor/ --output=SVG_Editor-final.zip final
```

The resulting `SVG_Editor-final.zip` contains exactly the tree selected by `final`. A
clone that previously fetched an older `final` must use the force-refresh command above;
without it, `git pull` leaves the stale tag in place even when it updates `master`.
