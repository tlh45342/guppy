#!/usr/bin/env python3
"""Fail closed on repository content Guppy does not intend to distribute."""
from pathlib import Path
import subprocess, sys

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN_SUFFIXES = {'.deb', '.img', '.raw', '.vdi', '.vhd', '.iso', '.log', '.exe', '.o', '.d', '.a'}
FORBIDDEN_DIRS = {'build', 'bin'}
ALLOW = {'tests/media/guppy-reference.iso'}

def tracked_files():
    try:
        out = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT)
    except (OSError, subprocess.CalledProcessError):
        return None
    return [Path(x.decode(errors='surrogateescape')) for x in out.split(b'\0') if x]

def forbidden(p):
    if str(p).replace('\\','/') in ALLOW:
        return False
    parts = p.parts
    if parts and parts[0] in FORBIDDEN_DIRS:
        return True
    if len(parts) >= 3 and parts[0] == 'lib' and parts[1] == 'vdisk' and parts[2] == 'build':
        return True
    return p.suffix.lower() in FORBIDDEN_SUFFIXES

def main():
    files = tracked_files()
    if files is None:
        print('[REPO-AUDIT] git metadata unavailable; checking working tree only')
        files = [p.relative_to(ROOT) for p in ROOT.rglob('*') if p.is_file() and '.git' not in p.parts]
    bad = sorted(p for p in files if forbidden(p))
    if bad:
        print('[REPO-AUDIT] FAIL: generated/binary/package artifacts are tracked:')
        for p in bad: print('  ' + str(p).replace('\\','/'))
        print('[REPO-AUDIT] Remove these from Git tracking; .gitignore prevents them returning.')
        return 1
    print('[REPO-AUDIT] PASS: no forbidden tracked artifacts')
    return 0
if __name__ == '__main__': sys.exit(main())
