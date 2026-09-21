from pathlib import Path
import struct, sys
p=Path('ext2-mknod.img')
data=p.read_bytes()
base=2048*512
u16=lambda o: struct.unpack_from('<H',data,o)[0]
u32=lambda o: struct.unpack_from('<I',data,o)[0]
sb=base+1024
assert u16(sb+56)==0xEF53, 'EXT2 magic missing'
bs=1024 << u32(sb+24)
inodes_per_group=u32(sb+40)
inode_size=u16(sb+88) or 128
first_data=u32(sb+20)
gdt=base+(first_data+1)*bs

def inode_off(ino):
    g=(ino-1)//inodes_per_group; local=(ino-1)%inodes_per_group
    it=u32(gdt+g*32+8)
    return base+it*bs+local*inode_size

def inode(ino):
    o=inode_off(ino)
    return {'mode':u16(o), 'size':u32(o+4), 'blocks':u32(o+28),
            'ib':[u32(o+40+i*4) for i in range(15)]}

def children(ino):
    n=inode(ino); out={}
    for b in n['ib'][:12]:
        if not b: continue
        pos=base+b*bs; end=pos+bs
        while pos+8<=end:
            child=u32(pos); rec=u16(pos+4); nl=data[pos+6]; ft=data[pos+7]
            if rec<8 or pos+rec>end: break
            if child and nl:
                name=data[pos+8:pos+8+nl].decode('ascii')
                out[name]=(child,ft)
            pos+=rec
    return out
root=children(2)
dev_ino=root['dev'][0]
dev=children(dev_ino)
expected={'console':(3,0o020000,1,0),'null':(3,0o020000,1,3),'zero':(3,0o020000,1,5),'example':(4,0o060000,8,0)}
for name,(ft,typ,maj,minr) in expected.items():
    ino,gotft=dev[name]; n=inode(ino)
    assert gotft==ft, (name,'dirent type',gotft,ft)
    assert n['mode'] & 0o170000 == typ, (name,'inode type',oct(n['mode']))
    assert n['blocks']==0, (name,'allocated data sectors',n['blocks'])
    raw=n['ib'][0] if n['ib'][0] else n['ib'][1]
    if n['ib'][0]: gotmaj=(raw>>8)&0xff; gotmin=raw&0xff
    else: gotmaj=(raw>>8)&0xfff; gotmin=(raw&0xff)|((raw>>12)&0xfff00)
    assert (gotmaj,gotmin)==(maj,minr), (name,'device',gotmaj,gotmin)
print('[VERIFY] EXT2 device inodes, dirent types, major/minor, and zero data-block allocation PASS')
