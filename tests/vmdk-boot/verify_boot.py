from pathlib import Path
import re

MESSAGE1 = b"HELLO FROM GUPPY VMDK"
MESSAGE2 = b"VIRTUAL LBA 0 IS GOOD"

descriptor = Path("boot-test.vmdk")
extent = Path("boot-test-flat.vmdk")
boot = Path("boot.bin")

text = descriptor.read_text(errors="replace")
assert text.startswith("# Disk DescriptorFile"), "not a VMDK descriptor"
assert 'createType="vmfs"' in text, "unexpected VMDK createType"
m = re.search(r'^RW\s+(\d+)\s+VMFS\s+"([^"]+)"', text, re.M)
assert m, "missing VMFS flat extent line"
assert m.group(2) == extent.name, (m.group(2), extent.name)

boot_bytes = boot.read_bytes()
assert len(boot_bytes) == 512, len(boot_bytes)
assert boot_bytes[510:512] == b"\x55\xaa", boot_bytes[510:512]
assert MESSAGE1 in boot_bytes, "first boot message missing"
assert MESSAGE2 in boot_bytes, "second boot message missing"

with extent.open("rb") as f:
    lba0 = f.read(512)

assert lba0 == boot_bytes, "VMDK virtual LBA 0 differs from boot.bin"
assert lba0[510:512] == b"\x55\xaa"

print("VMDK descriptor/extent: PASS")
print("Boot sector size/signature: PASS")
print("Boot messages present: PASS")
print("VMDK LBA 0 byte-for-byte: PASS")
print("Manual VM expectation: HELLO FROM GUPPY VMDK / VIRTUAL LBA 0 IS GOOD")
