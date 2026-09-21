#!/usr/bin/env python3
"""Remove disposable Guppy regression artifacts under tests/.

The regression suite creates disk/container images and logs from scratch.  A
successful `make clean` should therefore leave none of these generated files
behind.  Source/fixture-oriented files such as .txt, .deb, .iso and .script are
not touched.

If a future regression intentionally needs a checked-in artifact with one of
these generated suffixes, add its tests/-relative path to .clean-preserve.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

GENERATED_SUFFIXES = {".img", ".vdi", ".raw", ".vhd", ".log"}
PRESERVE_FILE = ".clean-preserve"


def load_preserve(root: Path) -> set[str]:
    path = root / PRESERVE_FILE
    preserved: set[str] = set()
    if not path.exists():
        return preserved
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        preserved.add(Path(line).as_posix())
    return preserved


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".", help="tests directory to clean")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    root = Path(args.root).resolve()
    preserved = load_preserve(root)
    removed: list[str] = []

    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in GENERATED_SUFFIXES:
            continue
        rel = path.relative_to(root).as_posix()
        if rel in preserved:
            continue
        try:
            path.unlink()
            removed.append(rel)
        except OSError as exc:
            print(f"[CLEAN] failed to remove {rel}: {exc}", file=sys.stderr)
            return 1

    # The sweep is also the audit: anything matching the disposable set that
    # remains and is not explicitly preserved is a cleanup failure.
    leftovers: list[str] = []
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.suffix.lower() in GENERATED_SUFFIXES:
            rel = path.relative_to(root).as_posix()
            if rel not in preserved:
                leftovers.append(rel)

    if leftovers:
        print("[CLEAN] unexpected generated artifacts remain:", file=sys.stderr)
        for rel in leftovers:
            print(f"  {rel}", file=sys.stderr)
        return 1

    if args.verbose:
        print(f"[CLEAN] removed {len(removed)} disposable test artifact(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
