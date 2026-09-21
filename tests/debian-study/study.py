#!/usr/bin/env python3
"""Read-only Debian package reconnaissance using Guppy's own .deb parser.

This is deliberately an investigation harness, not an installer and not part of
`make test`.  It mounts the configured Debian ISO read-only, registers its
repository, asks Guppy to inspect maintainer scripts for a curated package set,
and writes a compact CSV/TXT inventory.  Maintainer scripts are never executed.
"""
from __future__ import annotations
import argparse, csv, re, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_ISO = r"H:\debian-13.6.0-amd64-netinst.iso"
SIGNALS = [
    "filesystem mutation commands",
    "dynamic-linker cache (ldconfig)",
    "alternatives management",
    "initramfs generation",
    "service/systemd interaction",
    "live /proc, /sys, or udev interaction",
    "dpkg/helper tooling",
]
FOCUS = [("dash", "preinst"), ("dash", "postinst"), ("dash", "prerm"), ("dash", "postrm")]

def load_packages(path: Path) -> list[str]:
    out = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        s = raw.strip()
        if s and not s.startswith("#") and s not in out:
            out.append(s)
    return out

def make_script(iso: str, packages: list[str]) -> str:
    lines = [
        "logfile debian-study.log",
        f"use -i {iso} /dev/b",
        "mount /dev/b /iso",
        "pkg repo add /iso",
        "pkg repo list",
        "echo === DEBIAN STUDY: maintainer-script inventory ===",
    ]
    for pkg in packages:
        lines += [f"echo === PACKAGE {pkg} ===", f"pkg lookup {pkg}", f"pkg scripts {pkg}"]
    lines.append("echo === DEBIAN STUDY: focused script bodies ===")
    for pkg, phase in FOCUS:
        lines += [f"echo === SCRIPT {pkg} {phase} ===", f"pkg script {pkg} {phase}"]
    lines += ["echo === DEBIAN STUDY COMPLETE ===", "logfile off"]
    return "\n".join(lines) + "\n"

def parse_log(text: str, packages: list[str]) -> list[dict[str, str]]:
    rows = []
    for i, pkg in enumerate(packages):
        marker = f"=== PACKAGE {pkg} ==="
        start = text.find(marker)
        if start < 0:
            rows.append({"package": pkg, "found": "no", "version": "", "scripts": "", "signals": "", "assessment": "NOT SCANNED"})
            continue
        end = len(text)
        if i + 1 < len(packages):
            n = text.find(f"=== PACKAGE {packages[i+1]} ===", start + len(marker))
            if n >= 0: end = n
        else:
            n = text.find("=== DEBIAN STUDY: focused script bodies ===", start)
            if n >= 0: end = n
        block = text[start:end]
        version = ""
        m = re.search(r"^Version:\s+(.+)$", block, re.M)
        if m: version = m.group(1).strip()
        m = re.search(r"^Scripts found:\s+(\d+)", block, re.M)
        scripts = m.group(1) if m else ""
        signals = [s for s in SIGNALS if s in block]
        found = "yes" if "Package:" in block and "Maintainer scripts:" in block else "no"
        if found == "no": assessment = "NOT FOUND/SCAN FAILED"
        elif scripts == "0": assessment = "NO MAINTAINER SCRIPTS"
        elif signals: assessment = "REVIEW"
        else: assessment = "SCRIPT PRESENT - LOW HEURISTIC SIGNAL"
        rows.append({"package": pkg, "found": found, "version": version, "scripts": scripts,
                     "signals": "; ".join(signals), "assessment": assessment})
    return rows

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--guppy", required=True)
    ap.add_argument("--iso", default=DEFAULT_ISO)
    ap.add_argument("--packages", default=str(HERE / "known-packages.txt"))
    ns = ap.parse_args()

    guppy = Path(ns.guppy)
    if not guppy.is_absolute():
        guppy = (Path.cwd() / guppy).resolve()
    packages = load_packages(Path(ns.packages))
    script = HERE / "study.generated.script"
    log = HERE / "debian-study.log"
    csv_path = HERE / "debian-study-summary.csv"
    txt_path = HERE / "debian-study-summary.txt"
    script.write_text(make_script(ns.iso, packages), encoding="utf-8")

    print(f"Debian study: {len(packages)} known/expected packages")
    print(f"ISO: {ns.iso}")
    print("Mode: read-only package/control/script reconnaissance; execution DISABLED")
    cp = subprocess.run([str(guppy), script.name], cwd=HERE)
    if cp.returncode != 0:
        print(f"Debian study stopped (guppy rc={cp.returncode}). See {log}", file=sys.stderr)
        return cp.returncode
    if not log.exists():
        print("Debian study did not produce debian-study.log", file=sys.stderr)
        return 2

    rows = parse_log(log.read_text(encoding="utf-8", errors="replace"), packages)
    with csv_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["package", "found", "version", "scripts", "assessment", "signals"])
        w.writeheader(); w.writerows(rows)
    counts = {}
    for r in rows: counts[r["assessment"]] = counts.get(r["assessment"], 0) + 1
    out = [
        "Guppy Debian package reconnaissance",
        f"ISO: {ns.iso}",
        f"Packages requested: {len(rows)}",
        "Maintainer script execution: DISABLED",
        "",
        "Assessment counts:",
    ]
    for k in sorted(counts): out.append(f"  {k}: {counts[k]}")
    out += ["", "Packages:"]
    for r in rows:
        out.append(f"  {r['package']:<22} {r['version']:<24} scripts={r['scripts'] or '?':<2} {r['assessment']}")
        if r["signals"]: out.append(f"      signals: {r['signals']}")
    out += ["", "Focused raw script bodies are appended to debian-study.log for dash preinst/postinst/prerm/postrm."]
    txt_path.write_text("\n".join(out) + "\n", encoding="utf-8")
    print("\n".join(out))
    print(f"\nFull evidence: {log}")
    print(f"CSV inventory: {csv_path}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
