from pathlib import Path
import io
import tarfile

OUT = Path("test.deb")

def ar_member(name: str, payload: bytes) -> bytes:
    # System V ar header: 60 bytes, even-byte member alignment.
    n = (name + "/").encode("ascii")
    header = (
        n.ljust(16, b" ") +
        b"0".ljust(12, b" ") +
        b"0".ljust(6, b" ") +
        b"0".ljust(6, b" ") +
        b"100644".ljust(8, b" ") +
        str(len(payload)).encode("ascii").ljust(10, b" ") +
        b"`\n"
    )
    result = header + payload
    if len(payload) & 1:
        result += b"\n"
    return result

def make_tar(files):
    bio = io.BytesIO()
    with tarfile.open(fileobj=bio, mode="w") as tf:
        for name, kind, mode, data in files:
            ti = tarfile.TarInfo(name)
            ti.mode = mode
            ti.uid = 0
            ti.gid = 0
            ti.mtime = 0
            if kind == "dir":
                ti.type = tarfile.DIRTYPE
                ti.size = 0
                tf.addfile(ti)
            else:
                payload = data.encode("utf-8")
                ti.type = tarfile.REGTYPE
                ti.size = len(payload)
                tf.addfile(ti, io.BytesIO(payload))
    return bio.getvalue()

control = make_tar([
    ("control", "file", 0o644,
     "Package: guppy-pkg-test\nVersion: 1.0\nArchitecture: amd64\n"
     "Description: Guppy pkg Phase 1A acceptance package\n"),
])

data = make_tar([
    ("usr/", "dir", 0o755, ""),
    ("usr/bin/", "dir", 0o755, ""),
    ("usr/bin/guppy-pkg-test", "file", 0o755, "#!/bin/sh\necho pkg-test\n"),
    ("etc/", "dir", 0o755, ""),
    ("etc/guppy-pkg-test.conf", "file", 0o644, "phase=1a\n"),
])

blob = b"!<arch>\n"
blob += ar_member("debian-binary", b"2.0\n")
blob += ar_member("control.tar", control)
blob += ar_member("data.tar", data)
OUT.write_bytes(blob)
print(f"created {OUT} ({len(blob)} bytes)")
