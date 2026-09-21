from pathlib import Path
import struct

IMAGE = Path("ntfs-windows.vhd")
LOGICAL = 64 * 1024 * 1024
FOOTER = 512
BPS = 512
START_LBA = 2048
END_LBA = 131038
SECTORS = END_LBA - START_LBA + 1
SPC = 8
CLUSTER = BPS * SPC

MS_BASIC = bytes.fromhex("a2a0d0ebe5b9334487c068b6b72699c7")


def fail(msg):
    print(f"FAIL: {msg}")
    raise SystemExit(1)


def le16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def le32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def le64(buf, off):
    return struct.unpack_from("<Q", buf, off)[0]


if not IMAGE.exists():
    fail("ntfs-windows.vhd was not created")
if IMAGE.stat().st_size != LOGICAL + FOOTER:
    fail(f"host size {IMAGE.stat().st_size} != {LOGICAL + FOOTER}")

with IMAGE.open("rb") as f:
    # Fixed VHD footer.
    f.seek(LOGICAL)
    footer = f.read(FOOTER)
    if footer[:8] != b"conectix":
        fail("VHD footer cookie missing")
    if struct.unpack_from(">I", footer, 60)[0] != 2:
        fail("VHD disk type is not fixed (2)")

    # GPT entry #1.
    f.seek(2 * BPS)
    ent = f.read(128)
    if ent[:16] != MS_BASIC:
        fail("GPT entry #1 is not Microsoft Basic Data")
    first, last = struct.unpack_from("<QQ", ent, 32)
    if (first, last) != (START_LBA, END_LBA):
        fail(f"GPT range {(first, last)} != {(START_LBA, END_LBA)}")

    # NTFS primary boot sector.
    part_off = START_LBA * BPS
    f.seek(part_off)
    boot = f.read(BPS)
    if boot[3:11] != b"NTFS    ":
        fail("NTFS OEM signature missing")
    if le16(boot, 11) != BPS or boot[13] != SPC:
        fail("unexpected NTFS bytes-per-sector/sectors-per-cluster")
    if le32(boot, 28) != START_LBA:
        fail("NTFS hidden-sectors field does not match GPT partition start")
    if le64(boot, 40) != SECTORS:
        fail("NTFS total-sectors field does not match GPT partition size")
    if boot[510:512] != b"\x55\xaa":
        fail("NTFS boot signature missing")

    mft_lcn = le64(boot, 48)
    mirror_lcn = le64(boot, 56)

    # Backup boot sector at the partition's final sector.
    f.seek(END_LBA * BPS)
    backup = f.read(BPS)
    if backup != boot:
        fail("NTFS backup boot sector differs from primary")

    # Pass-5 checkpoint: MFT records 0..8 exist as FILE records.
    mft_off = part_off + mft_lcn * CLUSTER
    for recno in range(9):
        f.seek(mft_off + recno * 1024)
        hdr = f.read(4)
        if hdr != b"FILE":
            fail(f"MFT record {recno} FILE signature missing")

    # MFTMirr's first record must also be present.
    f.seek(part_off + mirror_lcn * CLUSTER)
    if f.read(4) != b"FILE":
        fail("$MFTMirr record 0 FILE signature missing")

print("Windows probe fixed VHD footer: PASS")
print("Windows probe GPT Microsoft Basic Data: PASS")
print("Windows probe NTFS primary/backup boot sectors: PASS")
print("Windows probe NTFS MFT records 0-8: PASS")
print("Windows probe image preflight: PASS")
print("Artifact retained: ntfs-windows.vhd")
