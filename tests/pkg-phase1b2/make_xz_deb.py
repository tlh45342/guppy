import io, tarfile, lzma
from pathlib import Path

def ar_member(name, data):
    name=(name+"/").encode()
    h=(name.ljust(16,b" ")+b"0".ljust(12,b" ")+b"0".ljust(6,b" ")+
       b"0".ljust(6,b" ")+b"100644".ljust(8,b" ")+str(len(data)).encode().ljust(10,b" ")+b"`\n")
    return h+data+(b"\n" if len(data)&1 else b"")

bio=io.BytesIO()
with tarfile.open(fileobj=bio,mode="w") as t:
    d=tarfile.TarInfo("./usr/"); d.type=tarfile.DIRTYPE; d.mode=0o755; t.addfile(d)
    d=tarfile.TarInfo("./usr/bin/"); d.type=tarfile.DIRTYPE; d.mode=0o755; t.addfile(d)
    data=b"hello from xz\n"
    f=tarfile.TarInfo("./usr/bin/xz-test"); f.size=len(data); f.mode=0o755
    t.addfile(f,io.BytesIO(data))
data_xz=lzma.compress(bio.getvalue(),format=lzma.FORMAT_XZ)

cb=io.BytesIO()
with tarfile.open(fileobj=cb,mode="w") as t:
    data=b"Package: xz-test\nVersion: 1\nArchitecture: amd64\n"
    f=tarfile.TarInfo("./control"); f.size=len(data); f.mode=0o644
    t.addfile(f,io.BytesIO(data))
control_xz=lzma.compress(cb.getvalue(),format=lzma.FORMAT_XZ)

out=b"!<arch>\n"+ar_member("debian-binary",b"2.0\n")+ar_member("control.tar.xz",control_xz)+ar_member("data.tar.xz",data_xz)
Path("xz-test.deb").write_bytes(out)
print("created xz-test.deb",len(out),"bytes")
