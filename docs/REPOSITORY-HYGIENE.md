# Repository hygiene

Guppy keeps source-controlled material separate from generated test artifacts and
third-party package/media inputs.

## Distribution boundary

The Guppy repository may contain source code, scripts, documentation, test
recipes, and generators. It must not contain `.deb` package binaries, generated
disk images, ISO images, logs, compiler objects/libraries, or built executables.

Synthetic `.deb` fixtures used by package regression tests belong on Guppy-owned
reference media. Their provenance and purpose must be documented before they are
added to that media. Real Debian packages and Debian installation media remain
external inputs and are used only by explicitly labelled Debian integration or
study tests.

## Audit

Run:

    make repo-audit

The audit fails closed if Git tracks a forbidden generated/binary/package
artifact. `.gitignore` prevents newly-created artifacts from being added by
accident, but it cannot untrack files already present in Git history/index.
Existing tracked artifacts must therefore be removed from Git tracking as part
of repository cleanup.

## Test ownership

A regression-test directory should own its generated artifacts and provide local
`make test` and `make clean` entry points where practical. The top-level test
coordinator should coordinate these operations rather than maintain knowledge of
every individual image/log filename. `VERBOSE` controls successful housekeeping
messages, not whether housekeeping occurs or whether failures are visible.

## Synthetic package reference media

Synthetic Debian-format packages used by the ordinary regression suite are not
kept as persistent `.deb` files in the repository.  Their editable reference
source is external to the checkout.  During the current migration the Windows
default is `Z:\deb`; override it with `GUPPY_REFERENCE_ROOT` or the test
Makefile `REFERENCE_ROOT` variable.

Package regressions stage only the fixture files they need, run the existing
script, and remove the staged copies before returning.  Therefore a completed
`make test`, whether successful or failed, should leave no `.deb` files under
`tests/`.

The real Debian ISO remains a separate integration/study input.  Real Debian
package binaries are not copied into Guppy's synthetic reference media.

## Guppy reference ISO

`tests/media/guppy-reference.iso` is the one intentional ISO exception in the
repository. It contains only Guppy-owned synthetic regression fixtures and is
safe to distribute with Guppy. Normal package regression stages fixtures from
this ISO through Guppy's own ISO9660/VFS path; no workstation-specific Z: drive
is required. Real Debian installation media remains external and is never part
of the Guppy repository.
