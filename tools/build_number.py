"""Generate an embedded build counter, advanced on actual link attempts."""
import pathlib
import sys

source = pathlib.Path(sys.argv[1])
record = pathlib.Path(sys.argv[2])
previous = int(record.read_text().strip()) if record.exists() else 0
number = previous + 1
source.write_text(f"unsigned guppy_build_number(void) {{ return {number}u; }}\n")
record.write_text(f"{number:04d}\n")
