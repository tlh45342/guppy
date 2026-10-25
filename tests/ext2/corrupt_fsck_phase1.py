from pathlib import Path
import struct
root=Path(r'O:\guppy\tests\ext2')
src=root/'fsck-phase1.img'
dst=root/'fsck-phase1-corrupt.img'
data=bytearray(src.read_bytes())
start=2048*512
off=start+1024+12
old=struct.unpack_from('<I',data,off)[0]
struct.pack_into('<I',data,off,old+1)
dst.write_bytes(data)
print('corrupted superblock free blocks',old,'->',old+1)
