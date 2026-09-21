from pathlib import Path
import sys

p = Path(sys.argv[1] if len(sys.argv) > 1 else "test.log")
if not p.exists():
    print(f"FAIL: {p} was not created")
    sys.exit(1)

s = p.read_text(errors="replace")

required = [
    "Logging to test.log",
    ">> pkg info test.deb",
    "Format:          deb 2.0",
    "Control archive: control.tar",
    "Data archive:    data.tar",
    ">> pkg contents test.deb",
    "usr/bin/guppy-pkg-test",
    "etc/guppy-pkg-test.conf",
    ">> logfile off",
]

missing = [x for x in required if x not in s]
if missing:
    print("FAIL: expected output missing:")
    for x in missing:
        print("  ", x)
    print("\n--- test.log ---")
    print(s)
    sys.exit(1)

print("PASS: .deb parser, uncompressed data.tar walker, and Guppy logfile")
