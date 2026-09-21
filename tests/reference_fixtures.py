#!/usr/bin/env python3
"""Stage Guppy-owned synthetic package fixtures from guppy-reference.iso.

The repository contains no loose .deb fixtures.  The distributable reference
ISO is the authoritative synthetic-fixture store.  Staging is deliberately
performed by Guppy itself (ISO9660 -> VFS -> lget -> host test directory), so
normal regression also exercises Guppy's reference-media read path.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

MANIFEST: dict[str, list[tuple[str, str]]] = {
    "pkg": [("control/control_.deb", "control-test.deb")],
    "pkg-scripts": [("scripts/script_t.deb", "script-test.deb")],
    "pkg-script-show": [("scripts/script_t.deb", "script-test.deb")],
    "pkg-preunpack": [("preunpac/libc6_2_.deb", "libc6_2.41-12+deb13u3_amd64.deb")],
    "pkg-dash-preunpack": [("preunpac/dash_0_5.deb", "dash_0.5.12-12_amd64.deb")],
    "pkg-base-passwd-preunpack": [("preunpac/base_pas.deb", "base-passwd_3.6.7_amd64.deb")],
    "pkg-base-files-preunpack": [
        ("preunpac/libc6_2_.deb", "libc6_2.41-12+deb13u3_amd64.deb"),
        ("preunpac/base_fil.deb", "base-files_13.8+deb13u6_amd64.deb"),
        ("preunpac/mawk_1_3.deb", "mawk_1.3.4.20250131-1_amd64.deb"),
        ("preunpac/packages.", "Packages"),
    ],
    "pkg-libc6-configure": [
        ("configur/libc6_2_.deb", "libc6_2.41-12+deb13u3_amd64.deb"),
        ("configur/libc_bin.deb", "libc-bin_2.41-12+deb13u3_amd64.deb"),
    ],
    "pkg-repo-lifecycle": [
        ("scripts/script_t.deb", "script-test.deb"),
        ("scripts/packages.", "Packages1"),
        ("scripts/packages2.", "Packages2"),
    ],
    "pkg-auto-install": [
        ("planner/packages.", "Packages"),
        ("planner/auto_roo.deb", "auto-root_1.0-1_amd64.deb"),
        ("planner/dep_a_1_.deb", "dep-a_1.0-1_amd64.deb"),
        ("planner/cyc_a_1_.deb", "cyc-a_1.0-1_amd64.deb"),
        ("planner/cyc_b_1_.deb", "cyc-b_1.0-1_amd64.deb"),
    ],
    "pkg-audit": [
        ("audit/packages.", "Packages"),
        ("audit/audit_li.deb", "audit-lib_1.0-1_amd64.deb"),
        ("audit/audit_ro.deb", "audit-root_1.0-1_amd64.deb"),
    ],
}


def staged_names(test_name: str) -> list[str]:
    return [local for _source, local in MANIFEST.get(test_name, [])]


def clean(test_name: str, cwd: Path) -> None:
    for name in staged_names(test_name):
        path = cwd / name
        try:
            if path.is_file() or path.is_symlink():
                path.unlink()
        except FileNotFoundError:
            pass


def _script_quote(value: str) -> str:
    return '"' + value.replace('"', '\\"') + '"'


def stage(test_name: str, cwd: Path, guppy: str, reference_iso: Path) -> list[str]:
    entries = MANIFEST.get(test_name, [])
    if not entries:
        return []
    iso = reference_iso.resolve()
    if not iso.is_file():
        raise RuntimeError(f"Guppy reference ISO not found: {iso}")

    clean(test_name, cwd)
    lines = [
        f"use -i {_script_quote(str(iso))} /dev/r",
        "mount /dev/r /reference",
    ]
    for source, local in entries:
        lines.append(f"lget /reference/deb/{source} {_script_quote(local)}")

    fd, script_name = tempfile.mkstemp(prefix="guppy-reference-", suffix=".script", dir=cwd, text=True)
    os.close(fd)
    script = Path(script_name)
    try:
        script.write_text("\n".join(lines) + "\n", newline="\n")
        cp = subprocess.run(
            [guppy, script.name], cwd=cwd, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, errors="replace"
        )
        if cp.returncode != 0:
            clean(test_name, cwd)
            detail = (cp.stdout or "").strip()
            raise RuntimeError(
                f"Guppy could not stage {test_name} from {iso}"
                + (f":\n{detail}" if detail else "")
            )
    finally:
        try: script.unlink()
        except FileNotFoundError: pass

    missing = [local for _source, local in entries if not (cwd / local).is_file()]
    if missing:
        clean(test_name, cwd)
        raise RuntimeError("reference ISO staging produced no file(s): " + ", ".join(missing))
    return [local for _source, local in entries]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("action", choices=["stage", "clean", "list"])
    ap.add_argument("test_name")
    ap.add_argument("--cwd", default=".")
    ap.add_argument("--guppy", default=os.environ.get("GUPPY", "guppy.exe" if os.name == "nt" else "../bin/guppy"))
    ap.add_argument("--reference-iso", default=os.environ.get("GUPPY_REFERENCE_ISO", "../media/guppy-reference.iso"))
    ns = ap.parse_args()
    if ns.test_name not in MANIFEST:
        print(f"reference fixtures: no manifest for {ns.test_name}", file=sys.stderr); return 2
    cwd = Path(ns.cwd).resolve()
    if ns.action == "list":
        for source, local in MANIFEST[ns.test_name]: print(f"{source} -> {local}")
        return 0
    if ns.action == "clean": clean(ns.test_name, cwd); return 0
    try:
        staged = stage(ns.test_name, cwd, ns.guppy, Path(ns.reference_iso))
    except RuntimeError as exc:
        print(f"reference fixtures: {exc}", file=sys.stderr); return 1
    print(f"reference fixtures: staged {len(staged)} file(s) for {ns.test_name} from ISO")
    return 0

if __name__ == "__main__": raise SystemExit(main())
