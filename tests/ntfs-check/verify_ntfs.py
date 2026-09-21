#!/usr/bin/env python3
"""Read-only NTFS structural checker for Guppy formatter regression.

This deliberately does not import or share Guppy formatter code.  It parses
NTFS structures independently so formatter and checker cannot agree merely
because they share implementation helpers.
"""
from __future__ import annotations

import argparse
import shutil
import struct
from pathlib import Path

MS_BASIC = bytes.fromhex("a2a0d0ebe5b9334487c068b6b72699c7")
ATTR_STANDARD_INFORMATION = 0x10
ATTR_FILE_NAME = 0x30
ATTR_VOLUME_NAME = 0x60
ATTR_VOLUME_INFORMATION = 0x70
ATTR_DATA = 0x80
ATTR_INDEX_ROOT = 0x90
ATTR_END = 0xFFFFFFFF
FILE_IN_USE = 0x0001
FILE_DIRECTORY = 0x0002
FILE_ATTR_HIDDEN = 0x00000002
FILE_ATTR_SYSTEM = 0x00000004
FILE_ATTR_DIRECTORY = 0x10000000
INDEX_ENTRY_LAST = 0x0002
COLLATION_FILE_NAME = 0x00000001

EXPECTED_METADATA = {
    0: ("$MFT", False),
    1: ("$MFTMirr", False),
    2: ("$LogFile", False),
    3: ("$Volume", False),
    4: ("$AttrDef", False),
    5: (".", True),
    6: ("$Bitmap", False),
    7: ("$Boot", False),
    8: ("$BadClus", False),
}

EXPECTED_ROOT_INDEX = [
    (4, "$AttrDef"),
    (6, "$Bitmap"),
    (2, "$LogFile"),
    (0, "$MFT"),
    (1, "$MFTMirr"),
    (3, "$Volume"),
]


class CheckError(Exception):
    pass


def u16(b: bytes, o: int) -> int:
    return struct.unpack_from("<H", b, o)[0]


def u32(b: bytes, o: int) -> int:
    return struct.unpack_from("<I", b, o)[0]


def u64(b: bytes, o: int) -> int:
    return struct.unpack_from("<Q", b, o)[0]


def require(cond: bool, msg: str) -> None:
    if not cond:
        raise CheckError(msg)


def discover_gpt_partition(f, image_bytes: int) -> tuple[int, int]:
    require(image_bytes >= 34 * 512, "image too small for GPT")
    f.seek(512)
    hdr = f.read(512)
    require(hdr[:8] == b"EFI PART", "GPT header signature missing")
    header_size = u32(hdr, 12)
    require(92 <= header_size <= 512, f"invalid GPT header size {header_size}")
    entries_lba = u64(hdr, 72)
    count = u32(hdr, 80)
    entry_size = u32(hdr, 84)
    require(128 <= entry_size <= 4096, f"invalid GPT entry size {entry_size}")
    require(count > 0, "GPT has no partition slots")

    for i in range(min(count, 4096)):
        f.seek(entries_lba * 512 + i * entry_size)
        ent = f.read(entry_size)
        require(len(ent) == entry_size, "short GPT partition entry read")
        if ent[:16] == MS_BASIC:
            first = u64(ent, 32)
            last = u64(ent, 40)
            require(first > 0 and last >= first, "invalid Microsoft Basic Data LBA range")
            require((last + 1) * 512 <= image_bytes, "partition extends past host image")
            return first, last
    raise CheckError("no Microsoft Basic Data GPT partition found")


def decode_record_size(code: int, cluster_bytes: int) -> int:
    signed = code if code < 128 else code - 256
    return (1 << (-signed)) if signed < 0 else signed * cluster_bytes


def restore_and_check_usa(raw: bytes, sector_bytes: int) -> bytes:
    require(raw[:4] == b"FILE", "FILE record magic missing")
    usa_ofs = u16(raw, 4)
    usa_count = u16(raw, 6)
    require(usa_count >= 2, f"invalid USA count {usa_count}")
    require(usa_ofs >= 8 and usa_ofs + usa_count * 2 <= len(raw), "USA array outside FILE record")
    sectors = usa_count - 1
    require(sectors * sector_bytes == len(raw),
            f"USA sector count {sectors} inconsistent with record size {len(raw)}")

    restored = bytearray(raw)
    usn = u16(raw, usa_ofs)
    require(usn != 0, "USA update sequence number is zero")
    for i in range(sectors):
        tail = (i + 1) * sector_bytes - 2
        found = u16(raw, tail)
        require(found == usn,
                f"USA fixup mismatch in FILE sector {i}: tail=0x{found:04x}, expected 0x{usn:04x}")
        original = u16(raw, usa_ofs + 2 + i * 2)
        struct.pack_into("<H", restored, tail, original)
    return bytes(restored)


def decode_runlist(run: bytes, recno: int) -> list[tuple[int, int]]:
    runs: list[tuple[int, int]] = []
    pos = 0
    current_lcn = 0
    while True:
        require(pos < len(run), f"MFT record {recno}: unterminated runlist")
        header = run[pos]
        pos += 1
        if header == 0:
            break
        len_n = header & 0x0F
        off_n = header >> 4
        require(1 <= len_n <= 8 and 1 <= off_n <= 8,
                f"MFT record {recno}: invalid runlist field widths")
        require(pos + len_n + off_n <= len(run), f"MFT record {recno}: truncated runlist")
        count = int.from_bytes(run[pos:pos+len_n], "little", signed=False)
        pos += len_n
        delta = int.from_bytes(run[pos:pos+off_n], "little", signed=True)
        pos += off_n
        require(count > 0, f"MFT record {recno}: zero-length run")
        current_lcn += delta
        require(current_lcn >= 0, f"MFT record {recno}: negative LCN")
        runs.append((current_lcn, count))
    require(runs, f"MFT record {recno}: empty nonresident runlist")
    return runs


def parse_attributes(restored: bytes, recno: int, attr_ofs: int, used: int) -> list[dict]:
    attrs: list[dict] = []
    off = attr_ofs
    seen_ids: set[int] = set()
    while True:
        require(off + 4 <= used, f"MFT record {recno}: attribute list overruns used bytes")
        atype = u32(restored, off)
        if atype == ATTR_END:
            break
        require(off + 16 <= used, f"MFT record {recno}: truncated attribute header at 0x{off:x}")
        length = u32(restored, off + 4)
        nonresident = restored[off + 8]
        name_len = restored[off + 9]
        name_ofs = u16(restored, off + 10)
        flags = u16(restored, off + 12)
        attr_id = u16(restored, off + 14)
        require(length >= 24 and length % 8 == 0,
                f"MFT record {recno}: invalid attribute length {length}")
        require(off + length <= used, f"MFT record {recno}: attribute extends past used bytes")
        require(name_len == 0 and name_ofs == 0,
                f"MFT record {recno}: unexpected named attribute in format seed")
        require(attr_id not in seen_ids, f"MFT record {recno}: duplicate attribute id {attr_id}")
        seen_ids.add(attr_id)
        item = {"type": atype, "length": length, "flags": flags, "id": attr_id,
                "nonresident": bool(nonresident)}
        if nonresident == 0:
            require(length >= 24, f"MFT record {recno}: short resident attribute")
            value_len = u32(restored, off + 16)
            value_ofs = u16(restored, off + 20)
            require(value_ofs >= 24 and value_ofs + value_len <= length,
                    f"MFT record {recno}: resident value outside attribute")
            item["value"] = restored[off + value_ofs: off + value_ofs + value_len]
        else:
            require(nonresident == 1 and length >= 64,
                    f"MFT record {recno}: invalid nonresident attribute header")
            lowest_vcn = u64(restored, off + 16)
            highest_vcn = u64(restored, off + 24)
            run_ofs = u16(restored, off + 32)
            allocated = u64(restored, off + 40)
            data_size = u64(restored, off + 48)
            initialized = u64(restored, off + 56)
            require(lowest_vcn == 0, f"MFT record {recno}: first extent lowest VCN is not zero")
            require(run_ofs >= 64 and run_ofs < length, f"MFT record {recno}: invalid runlist offset")
            require(initialized <= data_size <= allocated, f"MFT record {recno}: invalid nonresident sizes")
            runs = decode_runlist(restored[off + run_ofs:off + length], recno)
            run_clusters = sum(count for _, count in runs)
            require(highest_vcn + 1 == run_clusters,
                    f"MFT record {recno}: highest VCN inconsistent with runlist")
            item.update({"runs": runs, "allocated": allocated, "data_size": data_size,
                         "initialized": initialized, "lowest_vcn": lowest_vcn,
                         "highest_vcn": highest_vcn})
        attrs.append(item)
        off += length
    require(off + 4 <= used, f"MFT record {recno}: END marker outside used bytes")
    return attrs


def decode_file_name(value: bytes, recno: int) -> dict:
    require(len(value) >= 66, f"MFT record {recno}: $FILE_NAME too short")
    parent = u64(value, 0)
    allocated = u64(value, 40)
    real = u64(value, 48)
    file_attrs = u32(value, 56)
    name_len = value[64]
    namespace = value[65]
    require(66 + name_len * 2 <= len(value), f"MFT record {recno}: $FILE_NAME text overruns value")
    chars = []
    for i in range(name_len):
        code = u16(value, 66 + i * 2)
        require(code < 128, f"MFT record {recno}: checker fixture expects ASCII metadata names")
        chars.append(chr(code))
    return {
        "parent": parent & 0x0000FFFFFFFFFFFF,
        "allocated": allocated,
        "real": real,
        "file_attrs": file_attrs,
        "namespace": namespace,
        "name": "".join(chars),
    }


def decode_index_root(value: bytes) -> list[tuple[int, str]]:
    require(len(value) >= 32, "$INDEX_ROOT value too short")
    indexed_type = u32(value, 0)
    collation = u32(value, 4)
    index_block_size = u32(value, 8)
    clusters_per_index = value[12]
    require(indexed_type == ATTR_FILE_NAME,
            f"$INDEX_ROOT indexes attribute 0x{indexed_type:x}, expected $FILE_NAME")
    require(collation == COLLATION_FILE_NAME,
            f"$INDEX_ROOT collation {collation} != filename collation")
    require(index_block_size > 0 and (index_block_size & (index_block_size - 1)) == 0,
            f"$INDEX_ROOT invalid index block size {index_block_size}")
    require(clusters_per_index > 0, "$INDEX_ROOT clusters/index block is zero")

    hdr = 16
    entries_ofs = u32(value, hdr + 0)
    index_length = u32(value, hdr + 4)
    allocated = u32(value, hdr + 8)
    flags = value[hdr + 12]
    require(entries_ofs >= 16 and entries_ofs % 8 == 0,
            f"$INDEX_ROOT invalid entries offset {entries_ofs}")
    require(index_length >= entries_ofs + 16,
            f"$INDEX_ROOT invalid index length {index_length}")
    require(index_length <= allocated <= len(value) - hdr,
            "$INDEX_ROOT allocation/length outside resident value")
    require(flags == 0, "$INDEX_ROOT unexpectedly requires INDEX_ALLOCATION")

    pos = hdr + entries_ofs
    end = hdr + index_length
    found: list[tuple[int, str]] = []
    while True:
        require(pos + 16 <= end, "$INDEX_ROOT entry header overruns index")
        file_ref = u64(value, pos + 0)
        entry_len = u16(value, pos + 8)
        key_len = u16(value, pos + 10)
        entry_flags = u32(value, pos + 12)
        require(entry_len >= 16 and entry_len % 8 == 0,
                f"$INDEX_ROOT invalid entry length {entry_len}")
        require(pos + entry_len <= end, "$INDEX_ROOT entry overruns index")
        if entry_flags & INDEX_ENTRY_LAST:
            require(key_len == 0, "$INDEX_ROOT terminal entry has a key")
            require(pos + entry_len == end, "$INDEX_ROOT terminal entry is not last")
            break
        require(entry_flags == 0, f"$INDEX_ROOT unexpected entry flags 0x{entry_flags:x}")
        require(16 + key_len <= entry_len, "$INDEX_ROOT key overruns entry")
        key = value[pos + 16: pos + 16 + key_len]
        fn = decode_file_name(key, 5)
        recno = file_ref & 0x0000FFFFFFFFFFFF
        seq = file_ref >> 48
        require(seq == 1, f"$INDEX_ROOT entry {fn['name']!r}: sequence {seq} != 1")
        require(fn["parent"] == 5,
                f"$INDEX_ROOT entry {fn['name']!r}: parent {fn['parent']} != root record 5")
        found.append((recno, fn["name"]))
        pos += entry_len

    return found


def check_file_record(raw: bytes, recno: int, sector_bytes: int,
                      expected_name: str, expect_directory: bool) -> bytes:
    restored = restore_and_check_usa(raw, sector_bytes)
    seq = u16(restored, 16)
    links = u16(restored, 18)
    attr_ofs = u16(restored, 20)
    flags = u16(restored, 22)
    used = u32(restored, 24)
    allocated = u32(restored, 28)
    next_attr_id = u16(restored, 40)
    record_no = u32(restored, 44)

    require(seq != 0, f"MFT record {recno}: zero sequence number")
    require(links > 0, f"MFT record {recno}: zero hard-link count")
    require(flags & FILE_IN_USE, f"MFT record {recno}: not marked in-use")
    require(bool(flags & FILE_DIRECTORY) == expect_directory,
            f"MFT record {recno}: directory flag mismatch")
    require(allocated == len(raw),
            f"MFT record {recno}: allocated size {allocated} != record size {len(raw)}")
    require(0 < used <= allocated, f"MFT record {recno}: invalid used size {used}")
    require(0x30 <= attr_ofs <= len(raw) - 4 and attr_ofs % 8 == 0,
            f"MFT record {recno}: invalid attribute offset 0x{attr_ofs:x}")
    require(record_no == recno,
            f"MFT record number field {record_no} != expected {recno}")

    attrs = parse_attributes(restored, recno, attr_ofs, used)
    types = [a["type"] for a in attrs]
    expected_types = [ATTR_STANDARD_INFORMATION, ATTR_FILE_NAME]
    if recno == 3:
        expected_types.extend([ATTR_VOLUME_NAME, ATTR_VOLUME_INFORMATION])
    if recno in (0, 1, 2, 4, 6, 7, 8):
        expected_types.append(ATTR_DATA)
    if recno == 5:
        expected_types.append(ATTR_INDEX_ROOT)
    require(types == expected_types,
            f"MFT record {recno}: unexpected attributes {[hex(t) for t in types]}")
    require(next_attr_id == len(expected_types),
            f"MFT record {recno}: next attribute id {next_attr_id} != {len(expected_types)}")

    si = attrs[0]["value"]
    require(len(si) == 48, f"MFT record {recno}: $STANDARD_INFORMATION length {len(si)} != 48")
    si_attrs = u32(si, 32)
    require(si_attrs & FILE_ATTR_HIDDEN, f"MFT record {recno}: metadata file not hidden")
    require(si_attrs & FILE_ATTR_SYSTEM, f"MFT record {recno}: metadata file not system")

    fn = decode_file_name(attrs[1]["value"], recno)
    require(fn["name"] == expected_name,
            f"MFT record {recno}: $FILE_NAME {fn['name']!r} != {expected_name!r}")
    require(fn["parent"] == 5,
            f"MFT record {recno}: parent MFT reference {fn['parent']} != root record 5")
    require(fn["namespace"] == 1, f"MFT record {recno}: unexpected filename namespace {fn['namespace']}")
    require(bool(fn["file_attrs"] & FILE_ATTR_DIRECTORY) == expect_directory,
            f"MFT record {recno}: $FILE_NAME directory attribute mismatch")
    return restored, attrs


def bitmap_allocated(bitmap: bytes, lcn: int) -> bool:
    return lcn // 8 < len(bitmap) and bool(bitmap[lcn // 8] & (1 << (lcn & 7)))


def check_image(path: Path, quiet: bool = False) -> dict[str, int]:
    image_bytes = path.stat().st_size
    with path.open("rb") as f:
        first_lba, last_lba = discover_gpt_partition(f, image_bytes)
        sectors = last_lba - first_lba + 1
        volume_off = first_lba * 512

        f.seek(volume_off)
        boot = f.read(512)
        require(len(boot) == 512, "short NTFS boot-sector read")
        require(boot[3:11] == b"NTFS    ", "NTFS OEM signature missing")
        bps = u16(boot, 11)
        spc = boot[13]
        hidden = u32(boot, 28)
        total = u64(boot, 40)
        mft_lcn = u64(boot, 48)
        mirror_lcn = u64(boot, 56)
        mft_code = boot[64]

        require(bps in (512, 1024, 2048, 4096), f"invalid bytes/sector {bps}")
        require(spc != 0 and (spc & (spc - 1)) == 0, f"sectors/cluster {spc} is not power-of-two")
        require(hidden == first_lba, f"hidden sectors {hidden} != partition start {first_lba}")
        require(total == sectors, f"NTFS total sectors {total} != GPT partition sectors {sectors}")
        require(boot[510:512] == b"\x55\xaa", "NTFS boot signature 55AA missing")

        cluster_bytes = bps * spc
        record_bytes = decode_record_size(mft_code, cluster_bytes)
        require(record_bytes >= bps and record_bytes % bps == 0,
                f"invalid MFT record size {record_bytes}")
        require(mft_lcn * cluster_bytes < sectors * bps, "$MFT LCN outside volume")
        require(mirror_lcn * cluster_bytes < sectors * bps, "$MFTMirr LCN outside volume")

        f.seek((first_lba + sectors - 1) * bps)
        backup = f.read(bps)
        require(backup[:512] == boot, "backup boot sector differs from primary")

        mft_off = volume_off + mft_lcn * cluster_bytes
        mirror_off = volume_off + mirror_lcn * cluster_bytes
        mft_records: dict[int, bytes] = {}
        mft_attrs: dict[int, list[dict]] = {}
        for recno, (name, is_dir) in EXPECTED_METADATA.items():
            f.seek(mft_off + recno * record_bytes)
            raw = f.read(record_bytes)
            require(len(raw) == record_bytes, f"short $MFT record {recno} read")
            restored, attrs = check_file_record(raw, recno, bps, name, is_dir)
            mft_records[recno] = restored
            mft_attrs[recno] = attrs

        root_index_attr = mft_attrs[5][-1]
        require(root_index_attr["type"] == ATTR_INDEX_ROOT and not root_index_attr["nonresident"],
                "root $INDEX_ROOT is missing or nonresident")
        root_entries = decode_index_root(root_index_attr["value"])
        require(root_entries == EXPECTED_ROOT_INDEX,
                f"root $INDEX_ROOT entries {root_entries!r} != expected {EXPECTED_ROOT_INDEX!r}")
        for recno, name in root_entries:
            require(recno in EXPECTED_METADATA, f"root $INDEX_ROOT references unknown record {recno}")
            require(EXPECTED_METADATA[recno][0] == name,
                    f"root $INDEX_ROOT name {name!r} disagrees with MFT record {recno}")

        # Pass 5: core system-file semantics.
        volume_name = mft_attrs[3][2]
        volume_info = mft_attrs[3][3]
        require(volume_name["type"] == ATTR_VOLUME_NAME and not volume_name["nonresident"],
                "$Volume $VOLUME_NAME missing/nonresident")
        require(volume_name["value"].decode("utf-16le") == "GUPPY", "$Volume name is not GUPPY")
        require(volume_info["type"] == ATTR_VOLUME_INFORMATION and not volume_info["nonresident"],
                "$Volume $VOLUME_INFORMATION missing/nonresident")
        vi = volume_info["value"]
        require(len(vi) == 12 and vi[8] == 3 and vi[9] == 1 and u16(vi, 10) == 0,
                "$Volume information is not clean NTFS 3.1")

        attrdef_data = mft_attrs[4][-1]
        logfile_data = mft_attrs[2][-1]
        boot_data = mft_attrs[7][-1]
        badclus_data = mft_attrs[8][-1]
        for label, data in (("$AttrDef", attrdef_data), ("$LogFile", logfile_data), ("$Boot", boot_data)):
            require(data["type"] == ATTR_DATA and data["nonresident"], f"{label} $DATA is not nonresident")
        require(badclus_data["type"] == ATTR_DATA and not badclus_data["nonresident"] and
                badclus_data["value"] == b"", "$BadClus empty $DATA seed invalid")

        require(boot_data["runs"] == [(0, 2)], f"unexpected $Boot runlist {boot_data['runs']}")
        f.seek(volume_off)
        boot_payload = f.read(boot_data["data_size"])
        require(len(boot_payload) == boot_data["data_size"] and boot_payload[:512] == boot,
                "$Boot data does not expose the primary boot sector")

        attrdef_lcn, attrdef_count = attrdef_data["runs"][0]
        require(attrdef_count == 1, "$AttrDef is not one cluster in structural seed")
        f.seek(volume_off + attrdef_lcn * cluster_bytes)
        attrdef = f.read(attrdef_data["data_size"])
        expected_defs = [
            ("$STANDARD_INFORMATION", ATTR_STANDARD_INFORMATION),
            ("$FILE_NAME", ATTR_FILE_NAME),
            ("$VOLUME_NAME", ATTR_VOLUME_NAME),
            ("$VOLUME_INFORMATION", ATTR_VOLUME_INFORMATION),
            ("$DATA", ATTR_DATA),
            ("$INDEX_ROOT", ATTR_INDEX_ROOT),
        ]
        for i, (expected_name, expected_type) in enumerate(expected_defs):
            e = attrdef[i*160:(i+1)*160]
            require(len(e) == 160, "$AttrDef entry truncated")
            rawname = e[:128].decode("utf-16le").split("\x00", 1)[0]
            require(rawname == expected_name and u32(e, 128) == expected_type,
                    f"$AttrDef entry {i} mismatch")

        logfile_lcn, logfile_count = logfile_data["runs"][0]
        require(logfile_count == 16, "$LogFile structural reservation is not 16 clusters")
        f.seek(volume_off + logfile_lcn * cluster_bytes)
        require(f.read(logfile_data["data_size"]) == b"\x00" * logfile_data["data_size"],
                "$LogFile structural seed is not zeroed")

        for recno in range(4):
            f.seek(mirror_off + recno * record_bytes)
            raw = f.read(record_bytes)
            require(len(raw) == record_bytes, f"short $MFTMirr record {recno} read")
            name, is_dir = EXPECTED_METADATA[recno]
            mirrored, _ = check_file_record(raw, recno, bps, name, is_dir)
            require(mirrored == mft_records[recno],
                    f"$MFTMirr record {recno} does not match $MFT")

        # Pass 3: independently decode the nonresident $DATA mappings.
        mft_data = mft_attrs[0][-1]
        mirror_data = mft_attrs[1][-1]
        bitmap_data = mft_attrs[6][-1]
        require(mft_data["type"] == ATTR_DATA and mft_data["nonresident"], "$MFT $DATA is not nonresident")
        require(mirror_data["type"] == ATTR_DATA and mirror_data["nonresident"], "$MFTMirr $DATA is not nonresident")
        require(bitmap_data["type"] == ATTR_DATA and bitmap_data["nonresident"], "$Bitmap $DATA is not nonresident")
        require(mft_data["runs"] == [(mft_lcn, 4)], f"unexpected $MFT runlist {mft_data['runs']}")
        require(mirror_data["runs"] == [(mirror_lcn, 1)], f"unexpected $MFTMirr runlist {mirror_data['runs']}")

        bitmap_lcn, bitmap_clusters = bitmap_data["runs"][0]
        require(bitmap_data["data_size"] == (sectors // spc + 7) // 8,
                "$Bitmap logical size does not match cluster count")
        f.seek(volume_off + bitmap_lcn * cluster_bytes)
        bitmap = f.read(bitmap_data["data_size"])
        require(len(bitmap) == bitmap_data["data_size"], "short $Bitmap data read")

        referenced: set[int] = {0, (sectors // spc) - 1}
        for data in (mft_data, mirror_data, bitmap_data, attrdef_data, logfile_data, boot_data):
            for lcn, count in data["runs"]:
                referenced.update(range(lcn, lcn + count))
        for lcn in sorted(referenced):
            require(bitmap_allocated(bitmap, lcn), f"$Bitmap marks referenced cluster {lcn} free")

    result = {
        "first_lba": first_lba,
        "last_lba": last_lba,
        "sectors": sectors,
        "bps": bps,
        "spc": spc,
        "cluster_bytes": cluster_bytes,
        "record_bytes": record_bytes,
        "mft_lcn": mft_lcn,
        "mirror_lcn": mirror_lcn,
    }
    if not quiet:
        print("GPT Microsoft Basic Data: PASS")
        print(f"NTFS volume: LBA {first_lba}..{last_lba} ({sectors} sectors)")
        print(f"NTFS geometry: {bps} B/sector, {spc} sectors/cluster, {cluster_bytes} B/cluster")
        print("NTFS primary/backup boot sectors: PASS")
        print(f"NTFS MFT record size: {record_bytes} bytes")
        print("NTFS MFT metadata records 0-8: PASS")
        print("NTFS resident $STANDARD_INFORMATION attributes: PASS")
        print("NTFS resident $FILE_NAME attributes: PASS")
        print("NTFS root $INDEX_ROOT namespace: PASS")
        print("NTFS core $Volume/$AttrDef/$LogFile/$Boot/$BadClus semantics: PASS")
        print("NTFS nonresident $DATA runlists: PASS")
        print("NTFS $Bitmap allocation cross-check: PASS")
        print("NTFS $MFTMirr records 0-3 USA/fixups: PASS")
        print("NTFS $MFT/$MFTMirr logical mirror comparison: PASS")
        print("NTFS structural checker: PASS")
    return result


def negative_self_test(source: Path, corrupt: Path) -> None:
    shutil.copyfile(source, corrupt)
    info = check_image(source, quiet=True)
    with corrupt.open("r+b") as f:
        volume_off = info["first_lba"] * info["bps"]
        mft_off = volume_off + info["mft_lcn"] * info["cluster_bytes"]
        tail = mft_off + info["bps"] - 2
        f.seek(tail)
        old = f.read(2)
        require(len(old) == 2, "negative test could not read FILE sector tail")
        f.seek(tail)
        f.write(bytes((old[0] ^ 0x01, old[1])))

    try:
        check_image(corrupt, quiet=True)
    except CheckError as exc:
        print(f"NTFS corruption rejection: PASS ({exc})")
        return
    raise CheckError("negative self-test failed: corrupted FILE record was accepted")


def main() -> int:
    ap = argparse.ArgumentParser(description="read-only independent NTFS format checker")
    ap.add_argument("image", nargs="?", default="ntfs-check.img")
    ap.add_argument("--no-negative-self-test", action="store_true",
                    help="skip deliberate corruption/rejection check")
    ap.add_argument("--corrupt-copy", default="ntfs-check-corrupt.img")
    ns = ap.parse_args()

    image = Path(ns.image)
    try:
        check_image(image)
        if not ns.no_negative_self_test:
            negative_self_test(image, Path(ns.corrupt_copy))
    except (OSError, CheckError) as exc:
        print(f"NTFS structural checker: FAIL: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
