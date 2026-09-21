from pathlib import Path
import struct, sys

P=Path("torture.vdi")
B=1<<20
UNALLOC=0xffffffff

def u32(b,o): return struct.unpack_from("<I",b,o)[0]
def u64(b,o): return struct.unpack_from("<Q",b,o)[0]

data=P.read_bytes()
if len(data)<512:
    raise SystemExit("FAIL: image shorter than VDI header")

sig=u32(data,0x40)
version=u32(data,0x44)
itype=u32(data,0x4c)
off_map=u32(data,0x154)
off_data=u32(data,0x158)
disk_size=u64(data,0x170)
block_size=u32(data,0x178)
blocks=u32(data,0x180)
allocated=u32(data,0x184)

checks=[]
def ck(ok,msg):
    checks.append((bool(ok),msg))
    print(("PASS" if ok else "FAIL")+": "+msg)

ck(sig==0xBEDA107F,"VDI signature")
ck(version==0x00010001,"VDI version 1.1")
ck(itype==1,"dynamic image type")
ck(disk_size==64*1024*1024,"64 MiB logical size")
ck(block_size==B,"1 MiB block size")
ck(blocks==64,"64 guest blocks")
ck(allocated==4,"exactly four physical blocks allocated")

m=[u32(data,off_map+i*4) for i in range(blocks)]
alloc=[(i,v) for i,v in enumerate(m) if v!=UNALLOC]
ck([i for i,v in alloc]==[0,1,3,63],"expected scattered guest blocks allocated")
phys=[v for i,v in alloc]
ck(len(set(phys))==len(phys),"no duplicate physical mappings")
ck(all(v<allocated for v in phys),"all physical mappings in allocated range")
ck(all(m[i]==UNALLOC for i in range(blocks) if i not in (0,1,3,63)),
   "all untouched guest blocks remain unallocated")

def guest(off,n):
    out=bytearray()
    while n:
        gi=off//B
        bo=off%B
        take=min(n,B-bo)
        mv=m[gi]
        if mv==UNALLOC:
            out.extend(b"\0"*take)
        else:
            po=off_data+mv*B+bo
            out.extend(data[po:po+take])
        off+=take; n-=take
    return bytes(out)

partial=b"PARTIAL!"
cross=b"CROSSING-BOUNDARY"
middle=bytes((i*37+11)&0xff for i in range(257))
tail=b"TAIL-"+bytes(range(32))

ck(guest(4096,len(partial))==partial,"partial write payload")
ck(guest(4080,16)==b"\0"*16,"bytes before partial write remain zero")
ck(guest(4096+len(partial),32)==b"\0"*32,"bytes after partial write remain zero")

cross_off=1048568
ck(guest(cross_off,len(cross))==cross,"cross-block write payload")
ck(guest(cross_off-32,32)==b"\0"*32,"cross-block predecessor bytes remain zero")
ck(guest(cross_off+len(cross),32)==b"\0"*32,"cross-block successor bytes remain zero")

mid_off=3145851
ck(guest(mid_off,len(middle))==middle,"scattered middle-block payload")
ck(guest(2*B,4096)==b"\0"*4096,"unallocated neighboring block reads as zero")
ck(guest(4*B,4096)==b"\0"*4096,"other neighboring block reads as zero")

tail_off=66060411
ck(guest(tail_off,len(tail))==tail,"near-end payload")
ck(guest(63*B,64)==b"\0"*64,"start of allocated tail block remains zero")

expected_host=off_data+allocated*B
ck(len(data)==expected_host,"host file growth equals metadata plus allocated blocks")
ck(len(data)<disk_size//8,"host file remains far below logical capacity")

bad=[m for ok,m in checks if not ok]
if bad:
    print("RESULT: FAIL (%d checks)"%len(bad))
    raise SystemExit(1)
print("RESULT: PASS (%d checks)"%len(checks))
