"""Disposable integration tests for read-only ext2 allocation checker."""
from pathlib import Path
import hashlib
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
GUPPY = ROOT / "bin" / "guppy.exe"
if not GUPPY.exists():
    GUPPY = ROOT / "bin" / "guppy"

def run(script, cwd):
    p = cwd / "test.script"
    p.write_text(script, encoding="utf-8")
    return subprocess.run([str(GUPPY), str(p)], cwd=cwd, capture_output=True, text=True)

def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

with tempfile.TemporaryDirectory(prefix="guppy-fsck-", dir=ROOT / "tests" / "ext2") as td:
    wd = Path(td)
    clean = run("""create clean.img --size 16MiB
use -i clean.img /dev/a
gpt init /dev/a
gpt add /dev/a --type linuxfs --name rootfs --start 1MiB --size 100%
partscan /dev/a
mkfs.ext2 /dev/a1
fsck.ext2 /dev/a1
""", wd)
    assert clean.returncode == 0 and "errors=0" in clean.stdout, (clean.stdout, clean.stderr)
    img = wd / "clean.img"
    before = digest(img)
    verify = run("use -i clean.img /dev/a\npartscan /dev/a\nfsck.ext2 /dev/a1\n", wd)
    assert verify.returncode == 0 and digest(img) == before, (verify.stdout, verify.stderr)
    data = bytearray(img.read_bytes())
    off = 2048 * 512 + 1024 + 12
    struct.pack_into("<I", data, off, struct.unpack_from("<I", data, off)[0] + 1)
    damaged = wd / "damaged.img"
    damaged.write_bytes(data)
    damaged_before = digest(damaged)
    fail = run("use -i damaged.img /dev/a\npartscan /dev/a\nfsck.ext2 /dev/a1\n", wd)
    assert fail.returncode != 0 and "superblock free blocks" in fail.stderr, (fail.stdout, fail.stderr)
    assert digest(damaged) == damaged_before, "checker modified damaged image"
    # Corrupt root inode mode while preserving allocation bitmaps and counters.
    root_bad = bytearray(img.read_bytes())
    partition_start = 2048 * 512
    superblock = partition_start + 1024
    block_size = 1024 << struct.unpack_from('<I', root_bad, superblock + 24)[0]
    descriptor = partition_start + (2 if block_size == 1024 else 1) * block_size
    inode_table = struct.unpack_from('<I', root_bad, descriptor + 8)[0]
    inode_size = struct.unpack_from('<H', root_bad, superblock + 88)[0] or 128
    root_mode = partition_start + inode_table * block_size + inode_size
    struct.pack_into('<H', root_bad, root_mode, 0)
    bad_inode = wd / 'bad-inode.img'
    bad_inode.write_bytes(root_bad)
    before_inode = digest(bad_inode)
    check_inode = run('use -i bad-inode.img /dev/a\npartscan /dev/a\nfsck.ext2 /dev/a1\n', wd)
    assert check_inode.returncode != 0 and 'inode 2: invalid file type' in check_inode.stderr, (check_inode.stdout, check_inode.stderr)
    assert digest(bad_inode) == before_inode, 'inode checker modified image'
    # Phase 3: duplicate the root directory's first direct block in its second slot.
    dup = bytearray(img.read_bytes())
    root_ptr = root_mode + 40
    first_block = struct.unpack_from('<I', dup, root_ptr)[0]
    assert first_block != 0, 'root directory has no direct data block'
    struct.pack_into('<I', dup, root_ptr + 4, first_block)
    duplicate = wd / 'duplicate-block.img'
    duplicate.write_bytes(dup)
    duplicate_hash = digest(duplicate)
    check_duplicate = run('use -i duplicate-block.img /dev/a\npartscan /dev/a\nfsck.ext2 /dev/a1\n', wd)
    assert check_duplicate.returncode != 0 and 'already owned by inode 2' in check_duplicate.stderr, (check_duplicate.stdout, check_duplicate.stderr)
    assert digest(duplicate) == duplicate_hash, 'duplicate-block checker modified image'
    # Phase 3: an out-of-range block number must be rejected without reading it.
    outside = bytearray(img.read_bytes())
    maxblocks = struct.unpack_from('<I', outside, superblock + 4)[0]
    struct.pack_into('<I', outside, root_ptr, maxblocks)
    outside_image = wd / 'outside-block.img'
    outside_image.write_bytes(outside)
    outside_hash = digest(outside_image)
    check_outside = run('use -i outside-block.img /dev/a\npartscan /dev/a\nfsck.ext2 /dev/a1\n', wd)
    assert check_outside.returncode != 0 and 'out of bounds' in check_outside.stderr, (check_outside.stdout, check_outside.stderr)
    assert digest(outside_image) == outside_hash, 'out-of-bounds checker modified image'
    # Phase 4: inject an allocated, otherwise unreferenced block as an indirect table.
    indirect = bytearray(img.read_bytes())
    bitmap_block = struct.unpack_from('<I', indirect, descriptor)[0]
    bitmap_off = partition_start + bitmap_block * block_size
    first_data = struct.unpack_from('<I', indirect, superblock + 20)[0]
    total_blocks = struct.unpack_from('<I', indirect, superblock + 4)[0]
    candidate = next(b for b in range(first_data, total_blocks)
                     if not (indirect[bitmap_off + (b-first_data)//8] & (1 << ((b-first_data)%8))))
    indirect[bitmap_off + (candidate-first_data)//8] |= 1 << ((candidate-first_data)%8)
    # Maintain bitmap/descriptor/superblock accounting so only pointer traversal fails.
    struct.pack_into('<I', indirect, superblock+12, struct.unpack_from('<I',indirect,superblock+12)[0]-1)
    struct.pack_into('<H', indirect, descriptor+12, struct.unpack_from('<H',indirect,descriptor+12)[0]-1)
    struct.pack_into('<I', indirect, root_ptr+12*4, candidate)
    struct.pack_into('<I', indirect, partition_start+candidate*block_size, total_blocks)
    indirect_image = wd / 'bad-indirect.img'
    indirect_image.write_bytes(indirect)
    indirect_hash = digest(indirect_image)
    check_indirect = run('use -i bad-indirect.img /dev/a\npartscan /dev/a\nfsck.ext2 /dev/a1\n', wd)
    assert check_indirect.returncode != 0 and 'out of bounds' in check_indirect.stderr, (check_indirect.stdout,check_indirect.stderr)
    assert digest(indirect_image) == indirect_hash, 'indirect checker modified image'
    # Exercise the same recursive walker at depths two and three.
    for depth, slot in ((2, 13), (3, 14)):
        trial = bytearray(img.read_bytes())
        available = [b for b in range(first_data, total_blocks)
                     if not (trial[bitmap_off + (b-first_data)//8] & (1 << ((b-first_data)%8)))][:depth]
        assert len(available) == depth
        for b in available:
            trial[bitmap_off + (b-first_data)//8] |= 1 << ((b-first_data)%8)
        struct.pack_into('<I', trial, superblock+12, struct.unpack_from('<I',trial,superblock+12)[0]-depth)
        struct.pack_into('<H', trial, descriptor+12, struct.unpack_from('<H',trial,descriptor+12)[0]-depth)
        struct.pack_into('<I', trial, root_ptr+slot*4, available[0])
        for k, b in enumerate(available):
            child = available[k+1] if k+1 < depth else total_blocks
            struct.pack_into('<I', trial, partition_start+b*block_size, child)
        path = wd / f'bad-depth-{depth}.img'
        path.write_bytes(trial)
        original = digest(path)
        result = run(f'use -i {path.name} /dev/a\npartscan /dev/a\nfsck.ext2 /dev/a1\n', wd)
        assert result.returncode != 0 and 'out of bounds' in result.stderr, (depth,result.stdout,result.stderr)
        assert digest(path) == original, f'depth {depth} checker modified image'
    # Accounting: an allocated block with no inode or metadata owner is a leak.
    leaked = bytearray(img.read_bytes())
    leaked[bitmap_off+(candidate-first_data)//8] |= 1 << ((candidate-first_data)%8)
    struct.pack_into('<I',leaked,superblock+12,struct.unpack_from('<I',leaked,superblock+12)[0]-1)
    struct.pack_into('<H',leaked,descriptor+12,struct.unpack_from('<H',leaked,descriptor+12)[0]-1)
    leaked_path=wd/'leaked-block.img'
    leaked_path.write_bytes(leaked)
    leaked_hash=digest(leaked_path)
    leaked_result=run('use -i leaked-block.img /dev/a\npartscan /dev/a\nfsck.ext2 /dev/a1\n',wd)
    assert leaked_result.returncode!=0 and 'allocated blocks unaccounted for' in leaked_result.stderr,(leaked_result.stdout,leaked_result.stderr)
    assert digest(leaked_path)==leaked_hash
    # A referenced data block marked free must be detected even with balanced counters.
    missing=bytearray(img.read_bytes())
    assert missing[bitmap_off+(first_block-first_data)//8] & (1<<((first_block-first_data)%8))
    missing[bitmap_off+(first_block-first_data)//8] &= ~(1<<((first_block-first_data)%8))
    struct.pack_into('<I',missing,superblock+12,struct.unpack_from('<I',missing,superblock+12)[0]+1)
    struct.pack_into('<H',missing,descriptor+12,struct.unpack_from('<H',missing,descriptor+12)[0]+1)
    missing_path=wd/'referenced-free.img'
    missing_path.write_bytes(missing)
    missing_hash=digest(missing_path)
    missing_result=run('use -i referenced-free.img /dev/a\npartscan /dev/a\nfsck.ext2 /dev/a1\n',wd)
    assert missing_result.returncode!=0 and 'marked free' in missing_result.stderr,(missing_result.stdout,missing_result.stderr)
    assert digest(missing_path)==missing_hash
    # Directory entry points to an inode outside the inode table.
    bad_dir=bytearray(img.read_bytes())
    dirent=partition_start+first_block*block_size
    struct.pack_into('<I',bad_dir,dirent,struct.unpack_from('<I',bad_dir,superblock)[0]+1)
    bad_dir_path=wd/'bad-directory-entry.img'
    bad_dir_path.write_bytes(bad_dir)
    h=digest(bad_dir_path)
    check=run('use -i bad-directory-entry.img /dev/a\npartscan /dev/a\nfsck.ext2 /dev/a1\n',wd)
    assert check.returncode!=0 and 'unallocated inode' in check.stderr,(check.stdout,check.stderr)
    assert digest(bad_dir_path)==h
    # Link count differs from actual directory references.
    bad_links=bytearray(img.read_bytes())
    struct.pack_into('<H',bad_links,root_mode+26,3)
    bad_links_path=wd/'bad-link-count.img'
    bad_links_path.write_bytes(bad_links)
    h=digest(bad_links_path)
    check=run('use -i bad-link-count.img /dev/a\npartscan /dev/a\nfsck.ext2 /dev/a1\n',wd)
    assert check.returncode!=0 and 'directory references' in check.stderr,(check.stdout,check.stderr)
    assert digest(bad_links_path)==h
    print('PASS counters, inode, direct, indirect depths 1-3, ownership, directory references, link counts, read-only')
