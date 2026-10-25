from pathlib import Path
import sys

p = Path("bread.log")
if not p.exists():
    print("FAIL bread.log missing")
    sys.exit(1)
s = p.read_text(errors="replace").lower()
checks = [
    ("lba 0 (512 bytes)", "LBA 0 heading"),
    ("000001f0", "MBR tail offset"),
    ("55 aa", "MBR signature"),
    ("lba 1 (512 bytes)", "explicit LBA 1"),
    ("lba 2 (512 bytes)", "count=2 reaches LBA 2"),
    ("000003f0", "multi-sector output offset"),
]
for needle, label in checks:
    if needle not in s:
        print(f"FAIL {label}")
        sys.exit(1)
print("[VERIFY] bread logical block read, default count, multi-sector count, and MBR signature PASS")
