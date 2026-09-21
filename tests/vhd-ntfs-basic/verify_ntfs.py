from pathlib import Path
import struct
import sys

IMAGE = Path("fixed-ntfs.vhd")
LOGICAL = 1024 * 1024 * 1024
FOOTER = 512
START_LBA = 2048
END_LBA = 2097118
SECTORS = END_LBA - START_LBA + 1
BPS = 512
SPC = 8
CLUSTER = BPS * SPC


def fail(msg):
    print(f"FAIL: {msg}")
    raise SystemExit(1)


data = IMAGE.read_bytes() if IMAGE.stat().st_size < 64 * 1024 * 1024 else None
with IMAGE.open("rb") as f:
    if IMAGE.stat().st_size != LOGICAL + FOOTER:
        fail(f"host size {IMAGE.stat().st_size} != {LOGICAL + FOOTER}")

    f.seek(LOGICAL)
    footer = f.read(FOOTER)
    if footer[:8] != b"conectix":
        fail("VHD footer cookie missing")
    if struct.unpack(">I", footer[60:64])[0] != 2:
        fail("VHD disk type is not fixed (2)")

    # GPT entry 0: Microsoft Basic Data, 1 MiB aligned.
    f.seek(2 * 512)
    ent = f.read(128)
    msbasic = bytes.fromhex("a2a0d0ebe5b9334487c068b6b72699c7")
    if ent[:16] != msbasic:
        fail("GPT entry #1 is not Microsoft Basic Data")
    first, last = struct.unpack_from("<QQ", ent, 32)
    if (first, last) != (START_LBA, END_LBA):
        fail(f"GPT range {(first, last)} != {(START_LBA, END_LBA)}")

    # NTFS boot sector must be partition-relative, not at LBA0.
    f.seek(START_LBA * BPS)
    boot = f.read(BPS)
    if boot[3:11] != b"NTFS    ":
        fail("NTFS OEM signature missing at partition start")
    bps = struct.unpack_from("<H", boot, 11)[0]
    spc = boot[13]
    hidden = struct.unpack_from("<I", boot, 28)[0]
    total = struct.unpack_from("<Q", boot, 40)[0]
    mft_lcn = struct.unpack_from("<Q", boot, 48)[0]
    mirror_lcn = struct.unpack_from("<Q", boot, 56)[0]
    if (bps, spc) != (BPS, SPC):
        fail(f"NTFS geometry bps/spc {(bps, spc)} != {(BPS, SPC)}")
    if hidden != START_LBA:
        fail(f"hidden sectors {hidden} != partition start {START_LBA}")
    if total != SECTORS:
        fail(f"NTFS total sectors {total} != partition sectors {SECTORS}")
    if boot[510:512] != b"\x55\xaa":
        fail("NTFS boot signature missing")

    # Backup NTFS boot sector must be the final sector of the partition.
    f.seek(END_LBA * BPS)
    backup = f.read(BPS)
    if backup != boot:
        fail("NTFS backup boot sector does not match primary")

    # Prove both seeded metadata locations survived formatting.
    f.seek(START_LBA * BPS + mft_lcn * CLUSTER)
    if f.read(4) != b"FILE":
        fail("$MFT seed FILE record missing")
    f.seek(START_LBA * BPS + mirror_lcn * CLUSTER)
    if f.read(4) != b"FILE":
        fail("$MFTMirr seed FILE record missing")

print("VHD footer: PASS")
print("GPT Microsoft Basic Data: PASS")
print(f"NTFS partition start LBA: {START_LBA}")
print(f"NTFS total sectors: {SECTORS}")
print("NTFS primary/backup boot sectors: PASS")
print("NTFS $MFT/$MFTMirr seed records: PASS")
print("VHD/GPT/NTFS probe layout: PASS")
