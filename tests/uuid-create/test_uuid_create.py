from pathlib import Path
import subprocess, sys

GUPPY = sys.argv[1]
UUID = "01234567-89ab-cdef-0123-456789abcdef"
RAW = bytes.fromhex(UUID.replace("-", ""))

def run(lines):
    p = subprocess.run([GUPPY], input="\n".join(lines)+"\n", text=True,
                       capture_output=True)
    if p.returncode != 0:
        print(p.stdout); print(p.stderr, file=sys.stderr); raise SystemExit(1)
    return p.stdout + p.stderr

run([f"create uuid-test.vdi 8M --uuid {UUID}",
     f"create uuid-test.vmdk 8M --vmdk-type flat --uuid {UUID}", "exit"])
vdi = Path("uuid-test.vdi").read_bytes()
if vdi[0x188:0x198] != RAW:
    raise SystemExit("FAIL VDI image UUID")
desc = Path("uuid-test.vmdk").read_text().lower()
if f'ddb.uuid.image="{UUID}"' not in desc:
    raise SystemExit("FAIL VMDK image UUID")
if 'ddb.uuid.modification="' not in desc:
    raise SystemExit("FAIL VMDK modification UUID missing")
if 'ddb.uuid.parent="00000000-0000-0000-0000-000000000000"' not in desc:
    raise SystemExit("FAIL VMDK parent UUID missing")
if 'ddb.uuid.parentmodification="00000000-0000-0000-0000-000000000000"' not in desc:
    raise SystemExit("FAIL VMDK parent modification UUID missing")

bad = subprocess.run([GUPPY], input="create bad.vdi 1M --uuid nope\nexit\n",
                     text=True, capture_output=True)
if Path("bad.vdi").exists():
    Path("bad.vdi").unlink()
    raise SystemExit("FAIL invalid UUID created an image")
if "invalid UUID" not in (bad.stdout + bad.stderr):
    raise SystemExit("FAIL invalid UUID was not rejected")
print("[VERIFY] VDI/VMDK explicit shared image UUID PASS")
