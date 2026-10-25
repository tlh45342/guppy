#!/bin/sh
# Native Linux XZ -> Debian dpkg-deb -> Guppy ext2 -> hash/fsck.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
GUPPY="$ROOT/bin/guppy"
command -v dpkg-deb >/dev/null
command -v sha256sum >/dev/null
[ -x "$GUPPY" ] || { echo 'Build native Linux Guppy first: make'; exit 1; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
mkdir -p "$TMP/stage/DEBIAN" "$TMP/stage/usr/bin" "$TMP/extract"
printf 'Package: guppy-port-test\nVersion: 1.0\nArchitecture: all\nMaintainer: Guppy Test <test@example.invalid>\nDescription: Linux portability validation\n' > "$TMP/stage/DEBIAN/control"
cp /bin/true "$TMP/stage/usr/bin/guppy-true"
"$GUPPY" dpkg-deb --build "$TMP/stage" "$TMP/test.deb" --xz
dpkg-deb --info "$TMP/test.deb"
dpkg-deb --contents "$TMP/test.deb"
dpkg-deb --extract "$TMP/test.deb" "$TMP/extract"
cmp /bin/true "$TMP/extract/usr/bin/guppy-true"
printf 'create %s/disk.img --size 64MiB\nuse -i %s/disk.img /dev/a\ngpt init /dev/a\ngpt add /dev/a --type linuxfs --name rootfs --start 1MiB --size 100%%\npartscan /dev/a\nmkfs.ext2 /dev/a1\nmount /dev/a1 /\npkg install local:%s/test.deb /\npkg status / guppy-port-test\nsha256 /usr/bin/guppy-true\nfsck.ext2 /dev/a1\n' "$TMP" "$TMP" "$TMP" > "$TMP/test.script"
"$GUPPY" "$TMP/test.script" > "$TMP/guppy.log"
grep -q 'errors=0' "$TMP/guppy.log"
EXPECTED=$(sha256sum /bin/true | cut -d ' ' -f 1)
grep -q "$EXPECTED  /usr/bin/guppy-true" "$TMP/guppy.log"
echo 'PASS Linux native Guppy XZ -> dpkg-deb -> ext2 -> SHA256 -> fsck'
