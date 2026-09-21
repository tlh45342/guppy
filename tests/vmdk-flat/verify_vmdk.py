from pathlib import Path
import re

p = Path("fixed-test.vmdk")
e = Path("fixed-test-flat.vmdk")
text = p.read_text(errors="replace")
assert text.startswith("# Disk DescriptorFile")
assert 'createType="vmfs"' in text
m = re.search(r'^RW\s+(\d+)\s+VMFS\s+"([^"]+)"', text, re.M)
assert m, "missing VMFS extent line"
assert m.group(2) == e.name
sectors = int(m.group(1))
assert sectors == 40960, sectors
assert e.stat().st_size == sectors * 512
with e.open("rb") as f:
    f.seek(512)
    assert f.read(8) == b"EFI PART"
print("VMDK descriptor: PASS")
print("VMDK flat extent size: PASS")
print("GPT inside flat extent: PASS")
print("VMDK flat layout: PASS")
