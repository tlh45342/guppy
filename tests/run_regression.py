#!/usr/bin/env python3
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys
from datetime import datetime

from reference_fixtures import clean as clean_reference_fixtures, stage as stage_reference_fixtures

HERE = Path(__file__).resolve().parent
LOGDIR = HERE / "logs"
REFERENCE_ISO = None

TESTS = {
    "app-printf": {
        "cwd": "app-printf",
        "script": "test.script",
        "cleanup": ["app-printf.log"],
        "title": "app_printf/app_vprintf output routing foundation",
        "verify_file": "app-printf.log",
        "verify_contains": [
            "Logging to app-printf.log",
            ">> logfile",
            "logfile: app-printf.log",
            ">> logfile off",
            "Logging stopped.",
        ],
    },
    "pkg": {
        "cwd": "pkg",
        "reference_fixtures": True,
        "script": "test.script",
        "cleanup": ["pkg.img", "pkg.log"],
        "title": "Debian package inspect / metadata / depends / offline install / dpkg state",
        "verify_file": "pkg.log",
        "verify_contains": [
            "Package file:    local:control-test.deb",
            "Format:          deb 2.0",
            "usr/share/guppy-control-test",
            "Package:         guppy-control-test",
            "Version:         1.2.3-1",
            "Depends:         libc6 (>= 2.38), base-files",
            "Depends:",
            "libc6 (>= 2.38)",
            "base-files",
            "pkg: installed 1 files, 0 directories",
            "pkg: recorded guppy-control-test 1.2.3-1 as unpacked",
            "pkg: dpkg state: status + .list + 1 control/info files",
            "pkg: maintainer scripts were recorded but NOT executed",
            "Package: guppy-control-test",
            "Status: install ok unpacked",
            "/usr/share/guppy-control-test",
            "Path: /target/var/lib/dpkg/info/guppy-control-test.postinst",
            "Mode: 755",
        ],
    },
    "pkg-scripts": {
        "cwd": "pkg-scripts",
        "reference_fixtures": True,
        "script": "test.script",
        "cleanup": ["pkg-scripts.log"],
        "title": "Debian maintainer-script read-only inventory / signal classification",
        "verify_file": "pkg-scripts.log",
        "verify_contains": [
            "Maintainer scripts: local:script-test.deb",
            "postinst",
            "Interpreter: /bin/sh",
            "filesystem mutation commands",
            "dynamic-linker cache (ldconfig)",
            "alternatives management",
            "initramfs generation",
            "service/systemd interaction",
            "live /proc, /sys, or udev interaction",
            "dpkg/helper tooling",
            "Assessment: REVIEW",
            "Execution: DISABLED",
        ],
    },
    "pkg-script-show": {
        "cwd": "pkg-script-show",
        "reference_fixtures": True,
        "script": "test.script",
        "cleanup": ["pkg-script-show.log"],
        "title": "Debian maintainer-script read-only line-numbered dissection",
        "verify_file": "pkg-script-show.log",
        "verify_contains": [
            "Package file: local:script-test.deb",
            "Maintainer script: postinst [postinst]",
            "Mode: READ-ONLY",
            "BEGIN SCRIPT (line numbered)",
            "ldconfig",
            "Interesting lines (heuristic index; context above is authoritative):",
            "Execution: DISABLED",
        ],
    },
    "pkg-repo-lifecycle": {
        "cwd": "pkg-repo-lifecycle",
        "reference_fixtures": True,
        "script": "test.script",
        "cleanup": ["repo-test.img", "pkg-repo-lifecycle.log"],
        "title": "Debian repository add/list/delete/clear + package-name path/script resolution",
        "verify_file": "pkg-repo-lifecycle.log",
        "verify_contains": [
            "pkg: repository /target/repo1: indexed 1 package records",
            "pkg: repository /target/repo2: indexed 1 package records",
            "Repositories: 2",
            "/target/repo1  (1 records)",
            "/target/repo2  (1 records)",
            "Package: script-test",
            "Path: /target/repo1/pool/main/s/script-test/script-test_1.0-1_amd64.deb",
            "Maintainer scripts: /target/repo1/pool/main/s/script-test/script-test_1.0-1_amd64.deb",
            "pkg: repository deleted: 1",
            "Repositories: 1",
            "Package: helper-test",
            "pkg: repository registry cleared",
            "Repositories: 0",
        ],
    },
    "pkg-repo-gzip": {
        "cwd": "pkg-repo-gzip",
        "script": "test.script",
        "cleanup": ["repo-gzip.img", "pkg-repo-gzip.log"],
        "title": "Debian repository Packages.gz discovery / built-in gzip inflate",
        "verify_file": "pkg-repo-gzip.log",
        "verify_contains": [
            "pkg: repository /target/repo: indexed 1 package records",
            "Repositories: 1",
            "Package: gzip-fixture",
            "Version: 1.2.3-1",
            "Filename: pool/main/g/gzip-fixture/gzip-fixture_1.2.3-1_amd64.deb",
            "/target/repo/pool/main/g/gzip-fixture/gzip-fixture_1.2.3-1_amd64.deb",
        ],
    },
    "pkg-plan": {
        "cwd": "pkg-plan",
        "script": "test.script",
        "cleanup": ["pkg-plan.img", "pkg-plan.log"],
        "title": "Debian repository index / installed visibility / read-only dependency planner",
        "verify_file": "pkg-plan.log",
        "verify_contains": [
            "pkg: repository /target/repo: indexed 11 package records",
            "Package: coreutils",
            "Filename: pool/main/c/coreutils/coreutils_9.7-3_amd64.deb",
            "libc6                           2.41-12+deb13u3                 unpacked",
            "[libgcc-s1, libc6]",
            "[openssl-provider-legacy, libssl3t64]",
            "Unpack plan:",
            "group:",
            "coreutils                       9.7-3                           unpack",
            "Plan valid: YES",
            "Packages in closure: 11",
        ],
    },
    "pkg-auto-install": {
        "cwd": "pkg-auto-install",
        "reference_fixtures": True,
        "script": "test.script",
        "cleanup": ["pkg-auto-install.img", "pkg-auto-install.log"],
        "title": "Repository-backed dependency plan + automatic offline unpack",
        "verify_file": "pkg-auto-install.log",
        "verify_contains": [
            "pkg: repository /target/repo: indexed 4 package records",
            "Plan valid: YES",
            "pkg: dependency plan valid: 4 packages in closure",
            "pkg: unpack dep-a 1.0-1",
            "pkg: unpacking dependency cycle (2 packages)",
            "pkg: unpack auto-root 1.0-1",
            "pkg: automatic unpack complete: 4 unpacked, 0 already present",
            "pkg: maintainer scripts were NOT executed; packages remain unpacked",
            "Status: install ok unpacked",
            "/target/usr/share/guppy-auto/auto-root.txt",
        ],
    },
    "pkg-audit": {
        "cwd": "pkg-audit",
        "reference_fixtures": True,
        "script": "test.script",
        "cleanup": ["pkg-audit.img", "pkg-audit.log"],
        "title": "Read-only closure-wide maintainer-script reconnaissance",
        "verify_file": "pkg-audit.log",
        "verify_contains": [
            "Maintainer-script closure audit",
            "Requested: audit-root 1.0-1",
            "Packages in closure: 2",
            "Package audit: audit-lib 1.0-1",
            "Package audit: audit-root 1.0-1",
            "Closure audit complete",
            "Packages inspected: 2",
            "Scan failures: 0",
            "Execution: DISABLED",
        ],
    },
    "pkg-preunpack": {
        "cwd": "pkg-preunpack",
        "reference_fixtures": True,
        "script": "test.script",
        "cleanup": ["pkg-preunpack.img", "pkg-preunpack.log"],
        "title": "Controlled libc6 fresh pre-unpack / dpkg-divert state",
        "verify_file": "pkg-preunpack.log",
        "verify_contains": [
            "pkg pre-unpack: libc6: controlled fresh-install preinst",
            "Shell execution: DISABLED",
            "diversion add --no-rename: /lib64 -> /.lib64.usr-is-merged (owner base-files)",
            "diversion add --no-rename: /lib64/ld-linux-x86-64.so.2 -> /lib64/ld-linux-x86-64.so.2.usr-is-merged (owner libc6)",
            "pkg pre-unpack: libc6 fresh-install actions complete",
            "Status: install ok unpacked",
            "/.lib64.usr-is-merged",
            "/lib64/ld-linux-x86-64.so.2.usr-is-merged",
            "Path: /target/usr/lib64/ld-linux-x86-64.so.2",
            "Control file: local:libc6_2.41-12+deb13u3_amd64.deb [preinst]",
        ],
    },
    "pkg-dash-preunpack": {
        "cwd": "pkg-dash-preunpack",
        "reference_fixtures": True,
        "script": "test.script",
        "cleanup": ["pkg-dash-preunpack.img", "pkg-dash-preunpack.log"],
        "title": "Controlled dash fresh pre-unpack / audited no-op",
        "verify_file": "pkg-dash-preunpack.log",
        "verify_contains": [
            "pkg pre-unpack: dash: controlled fresh-install preinst",
            "Shell execution: DISABLED",
            "pkg pre-unpack: dash fresh-install preinst has no actions; audited no-op",
            "pkg pre-unpack: dash fresh-install actions complete",
            "pkg: dash handoff: retired unowned bootstrap /usr/bin/sh; Debian now owns /usr/bin/sh -> dash",
            "Status: install ok unpacked",
            "Path: /target/usr/bin/dash",
            "dash-synthetic-marker",
        ],
    },
    "pkg-base-passwd-preunpack": {
        "cwd": "pkg-base-passwd-preunpack",
        "reference_fixtures": True,
        "script": "test.script",
        "cleanup": ["pkg-base-passwd-preunpack.img", "pkg-base-passwd-preserve.img", "pkg-base-passwd-preunpack.log"],
        "title": "Controlled base-passwd fresh pre-unpack / account master-file creation",
        "verify_file": "pkg-base-passwd-preunpack.log",
        "verify_contains": [
            "pkg pre-unpack: base-passwd: controlled fresh-install preinst",
            "Shell execution: DISABLED",
            "pkg pre-unpack: base-passwd created /etc/passwd from package master data",
            "pkg pre-unpack: base-passwd created /etc/group from package master data",
            "pkg pre-unpack: base-passwd fresh-install actions complete",
            "Status: install ok unpacked",
            "root:*:0:0:root:/root:/bin/bash",
            "nogroup:*:65534:",
            "Path: /target/usr/share/base-passwd/guppy-marker.txt",
            "pkg pre-unpack: base-passwd preserved existing /etc/passwd",
            "pkg pre-unpack: base-passwd preserved existing /etc/group",
            "guppy-existing-passwd",
            "guppy-existing-group",
        ],
    },
    "pkg-base-files-preunpack": {
        "cwd": "pkg-base-files-preunpack",
        "reference_fixtures": True,
        "script": "test.script",
        "cleanup": ["pkg-base-files-preunpack.img", "pkg-base-files-preunpack.log"],
        "title": "Controlled base-files fresh pre-unpack / merged-/usr / DEP17 diversions",
        "verify_file": "pkg-base-files-preunpack.log",
        "verify_contains": [
            "pkg pre-unpack: libc6: controlled fresh-install preinst",
            "diversion add --no-rename: /lib64 -> /.lib64.usr-is-merged (owner base-files)",
            "pkg: unpack mawk 1.3.4.20250131-1",
            "pkg: unpack base-files 13.8+deb13u6",
            "pkg pre-unpack: base-files: controlled fresh-install preinst",
            "pkg pre-unpack: base-files merged-/usr checks passed",
            "diversion add --no-rename: /lib32 -> /.lib32.usr-is-merged (owner base-files)",
            "diversion add --no-rename: /libo32 -> /.libo32.usr-is-merged (owner base-files)",
            "diversion add --no-rename: /libx32 -> /.libx32.usr-is-merged (owner base-files)",
            "pkg pre-unpack: base-files fresh-install actions complete",
            "pkg: trigger interest registered: /usr/lib32 -> base-files (interest-noawait)",
            "pkg: trigger interest registered: /usr/libo32 -> base-files (interest-noawait)",
            "pkg: trigger interest registered: /usr/libx32 -> base-files (interest-noawait)",
            "/usr/lib32 base-files/noawait",
            "/usr/libo32 base-files/noawait",
            "/usr/libx32 base-files/noawait",
            "/usr/lib32 -> base-files (interest-noawait)",
            "pkg: link /target/bin -> usr/bin (already present)",
            "pkg: link /target/lib -> usr/lib (already present)",
            "pkg: link /target/lib64 -> usr/lib64 (already present)",
            "pkg: link /target/sbin -> usr/sbin (already present)",
            "Status: install ok unpacked",
            "usr/bin",
            "usr/lib",
            "usr/lib64",
            "usr/sbin",
            "/.lib64.usr-is-merged",
            "/.lib32.usr-is-merged",
            "/.libo32.usr-is-merged",
            "/.libx32.usr-is-merged",
            "pkg: already present: mawk 1.3.4.20250131-1",
            "pkg: already present: base-files 13.8+deb13u6",
            "pkg: automatic unpack complete: 0 unpacked, 2 already present",
        ],
    },
    "pkg-libc6-configure": {
        "cwd": "pkg-libc6-configure",
        "reference_fixtures": True,
        "script": "test.script",
        "cleanup": ["pkg-libc6-configure.img", "pkg-libc6-configure.log"],
        "title": "Controlled libc-bin configure / native ldconfig / trigger processing",
        "verify_file": "pkg-libc6-configure.log",
        "verify_contains": [
            "pkg pre-unpack: libc6: controlled fresh-install preinst",
            "trigger interest registered: ldconfig -> libc-bin (interest-await)",
            "Registered trigger interests:",
            "ldconfig -> libc-bin (interest-await)",
            "libc6 fresh-install postinst: upgrade/live-system branches skipped",
            "queued no-await trigger: libc-upgrade",
            "queued no-await trigger: ldconfig",
            "recorded libc6 2.41-12+deb13u3 as installed",
            "pkg configure: libc-bin installed default /etc/nsswitch.conf",
            "pkg ldconfig: native offline cache generation for /target",
            "pkg ldconfig: wrote /etc/ld.so.cache:",
            "Format: glibc-ld.so.cache 1.1",
            "libguppy-r13.so.1 -> /usr/lib/x86_64-linux-gnu/libguppy-r13.so.1 flags=0x0303",
            "Cache validation: PASS",
            "recorded libc-bin 2.41-12+deb13u3 as installed",
            "pkg triggers: dispatch ldconfig -> libc-bin (triggered)",
            "pkg triggers: completed and retired pending trigger: ldconfig",
            "unsupported pending trigger 'libc-upgrade'; left pending",
            "libc-upgrade -",
            "Status: install ok installed",
        ],
    },
    "pkg-configure": {
        "cwd": "pkg-configure",
        "script": "test.script",
        "cleanup": ["pkg-configure.img", "pkg-configure.log"],
        "title": "Controlled offline package configure / tmpfiles d / no-await trigger / state transition",
        "verify_file": "pkg-configure.log",
        "verify_contains": [
            "Status: install ok unpacked",
            "Package configure: config-demo 1.0-1",
            "Shell execution: DISABLED",
            "skipped previous-version-only branch (fresh install)",
            "tmpfiles d /run/config-demo mode=0755 uid=0 gid=0",
            "queued no-await trigger: ldconfig",
            "recorded config-demo 1.0-1 as installed",
            "Status: install ok installed",
            "Path: /target/run/config-demo",
            "Mode: 755",
            "ldconfig -",
        ],
    },
    "pkg-plan-conflict": {
        "cwd": "pkg-plan-conflict",
        "script": "test.script",
        "cleanup": ["conflict.img", "pkg-plan-conflict.log"],
        "title": "Debian planner contradiction / unsatisfiable version constraint",
        "expected_rc": 1,
        "verify_file": "pkg-plan-conflict.log",
        "verify_contains": [
            "Requested:",
            "a 2",
            "Plan valid: NO",
            "Conflict: selected a = 2 but b requires a = 1",
        ],
    },
    "symlink": {
        "cwd": "symlink",
        "script": "test.script",
        "cleanup": ["symlink-test.img", "symlink-test.log"],
        "title": "EXT2 symlink readlink + final/intermediate/chained pathname traversal",
        "verify_file": "symlink-test.log",
        "verify_contains": [
            ">> readlink /target/bin",
            "usr/bin",
            ">> readlink /target/sbin",
            "usr/sbin",
            ">> readlink /target/etc/os-release",
            "../usr/lib/os-release",
            ">> stat /target/bin",
            "Path: /target/bin",
            "Type: dir",
            ">> stat /target/etc/os-release",
            "Path: /target/etc/os-release",
            "Type: file",
            ">> stat /target/sbin/init",
            "Path: /target/sbin/init",
            "debmin-init",
            ">> stat /target/bin/tool",
            "Path: /target/bin/tool",
            "symlink-tool",
            ">> stat /target/tools/tool",
            "Path: /target/tools/tool",
        ],
    },
    "file-ops": {
        "cwd": "file-ops",
        "script": "test.script",
        "cleanup": ["file-ops.img", "file-ops.log"],
        "title": "EXT2 common file operations / cwd / relative-path workflow",
        "verify_file": "file-ops.log",
        "verify_contains": [
            "/target/work",
            "alpha-version",
            "Path: source.txt",
            "Type: file",
            "Mode: 644",
            "recreated-copy",
            "/target/work/nested",
        ],
    },
    "ext2-mknod": {
        "cwd": "ext2-mknod",
        "script": "create.script",
        "cleanup": ["ext2-mknod.img", "ext2-mknod-create.log", "ext2-mknod-reopen.log"],
        "title": "EXT2 character/block special-device inode creation",
        "verify_file": "ext2-mknod-create.log",
        "verify_contains": [
            "Type: char",
            "Major: 1",
            "Minor: 3",
            "Type: block",
            "Major: 8",
            "Blocks: 0",
        ],
    },
    "rm": {
        "cwd": "rm",
        "script": "test.script",
        "cleanup": ["rm-test.img", "rm-test.log"],
        "title": "EXT2 rm / unlink + recreate + neighboring-file preservation",
        "verify_file": "rm-test.log",
        "verify_contains": [
            "first-version",
            ">> rm /target/work/delete-me.txt",
            "recreated-version",
            "keep-version",
            "keep.txt",
        ],
    },
    "device-registry": {
        "cwd": "device-registry",
        "script": "test.script",
        "cleanup": ["first.img", "second.img", "device-registry.log"],
        "title": "device reattach / rescan stale-child cleanup",
        "verify_file": "device-registry.log",
        "verify_contains": [
            "/dev/a     /dev/a",
            "partscan: no partitions registered on /dev/a",
        ],
        "verify_not_contains": [
            "/dev/a1",
        ],
    },
    "gpt": {
        "cwd": "gpt",
        "script": "test.script",
        "cleanup": ["gpt.img", "gpt.log"],
        "title": "GPT create / add / overlap rejection / reread geometry",
        "verify_file": "gpt.log",
        "verify_contains": [
            "Initialized GPT on /dev/a",
            "Added linuxfs 'rootfs' at [2048, 8191]",
            "Added linuxfs 'data' at [8192, 16383]",
            "/dev/a1    /dev/a                   start=2048 size=6144 LBAs",
            "/dev/a2    /dev/a                   start=8192 size=8192 LBAs",
            "  1          2048          8191",
            "  2          8192         16383",
        ],
        "verify_not_contains": [
            "Added linuxfs 'overlap'",
            "/dev/a3",
            "  3          4096         12287",
        ],
    },
    "ext2-mount": {
        "cwd": "ext2-mount",
        "script": "test.script",
        "cleanup": ["ext2-mount.img", "ext2-mount.log"],
        "title": "EXT2 mkfs / auto-probe / mount / basic I/O",
        "verify_file": "ext2-mount.log",
        "verify_contains": [
            "mkfs.ext2: formatting /dev/a1",
            "label=GUPPYTEST",
            "mkfs.ext2: done",
            "/dev/a1    ext2",
            "/target",
            "ext2-probe-ok",
            "Path: /target/etc/probe.txt",
            "Type: file",
        ],
    },
    "ext2-inode-groups": {
        "cwd": "ext2-inode-groups",
        "script": "test.script",
        "cleanup": ["ext2-inode-groups.vdi", "ext2-inode-groups.log"],
        "title": "EXT2 VFS inode lookup/write across block-group boundary",
        "verify_file": "ext2-inode-groups.log",
        "verify_contains": [
            "label=INODEGROUP",
            "Path: /target/many/f1100",
            "Inode: 1111",
            "inode-group-1100",
            "Mode: 600",
        ],
    },
    "fat-format": {
        "cwd": "fat-format",
        "script": "test.script",
        "cleanup": ["fat-format.img", "vfat-format.img", "fat-format.log"],
        "title": "FAT/VFAT partition-safe formatting + GPT preservation",
        "verify_file": "fat-format.log",
        "verify_contains": [
            "Formatted FAT16 on /dev/a1",
            "Formatted VFAT on /dev/b1",
            "fatpart",
            "vfatpart",
            "/dev/a1",
            "/dev/b1",
        ],
    },
    "accounts": {
        "cwd": "accounts",
        "script": "test.script",
        "cleanup": ["accounts.img", "accounts.log"],
        "title": "offline useradd/passwd + shadow + EXT2 home ownership",
        "verify_file": "accounts.log",
        "verify_contains": [
            "useradd: created alice uid=1001 gid=1001",
            "passwd: updated password for alice",
            "alice:x:1001:1001:TestUser:/home/alice:/bin/sh",
            "alice:x:1001:",
            "alice:$5$",
            "Path: /target/home/alice",
            "UID: 1001",
            "GID: 1001",
            "useradd: created bob uid=1002 gid=1002",
            "bob:$5$testsalt$0nJaoNBqGOzZuuF0ljY7kJtJzaDPzCc.//FOE7ialA8",
        ],
    },
    "metadata": {
        "cwd": "metadata",
        "scripts": ["phase1.script", "phase2.script"],
        "cleanup": ["metadata.img", "metadata.log"],
        "title": "EXT2 chmod/stat persistence across remount",
        "verify_file": "metadata.log",
        "verify_contains": [
            "Path: /target/usr/bin/guppy-test",
            "Mode: 644",
            "Mode: 755",
            "Logging to metadata.log (append)",
            "Mode: 600",
            "metadata-persistence",
        ],
    },
    "utility-output": {
        "cwd": "utility-output",
        "scripts": ["phase1.script", "phase2.script"],
        "cleanup": ["utility.img", "raw-write.img", "utility-output.log"],
        "title": "utility/local bridge + raw write + logfile append routing",
        "verify_file": "utility-output.log",
        "verify_contains": [
            ">> version",
            "Guppy utility regression payload.",
            "host-input.txt",
            "ffffcfc3da693bf5285e97c1c3abfcb56deb691123f08ea1d71b5e536702a9b8  /target/data/host-input.txt",
            "00000000  47 75 70 70 79 20 75 74  69 6c 69 74 79 20 72 65",
            "Wrote 34 bytes from host-input.txt to /dev/b at +4096",
            "Logging to utility-output.log (append)",
            "logfile: utility-output.log",
        ],
    },
    "vmdk-flat": {
        "cwd": "vmdk-flat",
        "scripts": ["phase1.script", "phase2.script"],
        "cleanup": ["fixed-test.vmdk", "fixed-test-flat.vmdk", "vmdk-flat.log"],
        "title": "VMDK descriptor + flat extent / GPT / EXT2 / reopen persistence",
        "verify_file": "vmdk-flat.log",
        "verify_contains": [
            "Created fixed-test.vmdk (20971520 bytes) [vmdk, flat]",
            "Detected format:   VMDK",
            "Variant:           flat/external",
            "Container sanity:  PASS",
            "Initialized GPT on /dev/a",
            "Added linuxfs 'rootfs' at [2048, 40926]",
            "mkfs.ext2: formatting /dev/a1",
            "label=VMDKFLAT",
            "vmdk-flat-persistence",
            "Path: /target/etc/vmdk-test.txt",
            "Type:              GPT (protective MBR present)",
            "Result: CONTAINER VALID",
            "Logging to vmdk-flat.log (append)",
        ],
        "verify_script": "verify_vmdk.py",
        "verify_script_contains": [
            "VMDK descriptor: PASS",
            "VMDK flat extent size: PASS",
            "GPT inside flat extent: PASS",
            "VMDK flat layout: PASS",
        ],
        "verify_host_files": [
            {"path": "fixed-test.vmdk", "min_size": 128, "max_size": 4096},
            {"path": "fixed-test-flat.vmdk", "min_size": 20971520, "max_size": 20971520},
        ],
    },
    "vmdk-boot": {
        "cwd": "vmdk-boot",
        "script": "test.script",
        "cleanup": ["boot-test.vmdk", "boot-test-flat.vmdk", "vmdk-boot.log"],
        "title": "VMDK flat extent exact LBA-0 BIOS boot-sector placement",
        "verify_file": "vmdk-boot.log",
        "verify_contains": [
            "Created boot-test.vmdk (20971520 bytes) [vmdk, flat]",
            "Detected format:   VMDK",
            "Variant:           flat/external",
            "Wrote 512 bytes from boot.bin to /dev/a at +0",
            "Result: CONTAINER VALID",
        ],
        "verify_script": "verify_boot.py",
        "verify_script_contains": [
            "VMDK descriptor/extent: PASS",
            "Boot sector size/signature: PASS",
            "Boot messages present: PASS",
            "VMDK LBA 0 byte-for-byte: PASS",
        ],
        "verify_host_files": [
            {"path": "boot-test.vmdk", "min_size": 128, "max_size": 4096},
            {"path": "boot-test-flat.vmdk", "min_size": 20971520, "max_size": 20971520},
        ],
    },
    "vmdk-monolithic-flat-scaffold": {
        "cwd": "vmdk-layouts", "script": "monolithic-flat.script", "cleanup": ["not-yet.vmdk", "not-yet-flat.vmdk"],
        "title": "VMDK monolithic-flat CLI scaffold", "expected_rc": 2,
    },
    "vmdk-monolithic-sparse-scaffold": {
        "cwd": "vmdk-layouts", "script": "monolithic-sparse.script", "cleanup": ["not-yet.vmdk", "not-yet-flat.vmdk"],
        "title": "VMDK monolithic-sparse CLI scaffold", "expected_rc": 2,
    },
    "vmdk-split-flat-scaffold": {
        "cwd": "vmdk-layouts", "script": "split-flat.script", "cleanup": ["not-yet.vmdk", "not-yet-flat.vmdk"],
        "title": "VMDK split-flat CLI scaffold", "expected_rc": 2,
    },
    "vmdk-split-sparse-scaffold": {
        "cwd": "vmdk-layouts", "script": "split-sparse.script", "cleanup": ["not-yet.vmdk", "not-yet-flat.vmdk"],
        "title": "VMDK split-sparse CLI scaffold", "expected_rc": 2,
    },
    "ntfs-check": {
        "cwd": "ntfs-check",
        "script": "phase1.script",
        "cleanup": ["ntfs-check.img", "ntfs-check-corrupt.img", "ntfs-check.log"],
        "title": "independent NTFS formatter structural checker + corruption rejection",
        "verify_file": "ntfs-check.log",
        "verify_contains": [
            "Created ntfs-check.img",
            "Initialized GPT on /dev/a",
            "Added msbasic 'data'",
        ],
        "verify_script": "verify_ntfs.py",
        "verify_script_contains": [
            "GPT Microsoft Basic Data: PASS",
            "NTFS primary/backup boot sectors: PASS",
            "NTFS MFT metadata records 0-8: PASS",
            "NTFS core $Volume/$AttrDef/$LogFile/$Boot/$BadClus semantics: PASS",
            "NTFS nonresident $DATA runlists: PASS",
            "NTFS $Bitmap allocation cross-check: PASS",
            "NTFS resident $STANDARD_INFORMATION attributes: PASS",
            "NTFS resident $FILE_NAME attributes: PASS",
            "NTFS $MFTMirr records 0-3 USA/fixups: PASS",
            "NTFS $MFT/$MFTMirr logical mirror comparison: PASS",
            "NTFS structural checker: PASS",
            "NTFS corruption rejection: PASS",
        ],
    },
    "ntfs-windows": {
        "cwd": "ntfs-windows",
        "script": "build.script",
        "cleanup": ["ntfs-windows.vhd", "ntfs-windows.log"],
        "title": "external Windows fixed-VHD / GPT / NTFS Pass-5 interoperability probe",
        "verify_file": "ntfs-windows.log",
        "verify_contains": [
            "Created ntfs-windows.vhd (67108864 bytes) [vhd]",
            "Initialized GPT on /dev/a",
            "Added msbasic 'data' at [2048, 131038]",
            "Type:              GPT (protective MBR present)",
            "Result: CONTAINER VALID",
        ],
        "verify_script": "verify_windows_probe.py",
        "verify_script_contains": [
            "Windows probe fixed VHD footer: PASS",
            "Windows probe GPT Microsoft Basic Data: PASS",
            "Windows probe NTFS primary/backup boot sectors: PASS",
            "Windows probe NTFS MFT records 0-8: PASS",
            "Windows probe image preflight: PASS",
            "Artifact retained: ntfs-windows.vhd",
        ],
        "verify_host_files": [
            {"path": "ntfs-windows.vhd", "min_size": 67109376, "max_size": 67109376},
        ],
    },
    "vhd-ntfs-basic": {
        "cwd": "vhd-ntfs-basic",
        "scripts": ["phase1.script", "phase2.script"],
        "cleanup": ["fixed-ntfs.vhd", "vhd-ntfs-basic.log"],
        "title": "1 GiB fixed VHD / GPT Microsoft Basic Data / NTFS probe / reopen",
        "verify_file": "vhd-ntfs-basic.log",
        "verify_contains": [
            "Created fixed-ntfs.vhd (1073741824 bytes) [vhd]",
            "Detected format:   VHD",
            "Logical size:      1.00 GiB (1073741824 bytes)",
            "Initialized GPT on /dev/a",
            "Added msbasic 'data' at [2048, 2097118]",
            "msbasic",
            "Type:              GPT (protective MBR present)",
            "Result: CONTAINER VALID",
            "Logging to vhd-ntfs-basic.log (append)",
        ],
        "verify_script": "verify_ntfs.py",
        "verify_script_contains": [
            "VHD footer: PASS",
            "GPT Microsoft Basic Data: PASS",
            "NTFS primary/backup boot sectors: PASS",
            "NTFS $MFT/$MFTMirr seed records: PASS",
            "VHD/GPT/NTFS probe layout: PASS",
        ],
        "verify_host_files": [
            {"path": "fixed-ntfs.vhd", "min_size": 1073742336, "max_size": 1073742336},
        ],
    },
    "vhd-fixed": {
        "cwd": "vhd-fixed",
        "scripts": ["phase1.script", "phase2.script"],
        "cleanup": ["fixed-test.vhd", "vhd-fixed.log"],
        "title": "fixed VHD create / footer audit / partition / EXT2 / reopen persistence",
        "verify_file": "vhd-fixed.log",
        "verify_contains": [
            "Created fixed-test.vhd (20971520 bytes) [vhd]",
            "Detected format:   VHD",
            "Variant:           fixed/full",
            "Container sanity:  PASS",
            "Initialized GPT on /dev/a",
            "Added linuxfs 'rootfs' at [2048, 40926]",
            "mkfs.ext2: formatting /dev/a1",
            "label=VHDFIXED",
            "vhd-fixed-persistence",
            "Path: /target/etc/vhd-test.txt",
            "Type:              GPT (protective MBR present)",
            "Result: CONTAINER VALID",
            "Logging to vhd-fixed.log (append)",
        ],
        "verify_host_files": [
            {"path": "fixed-test.vhd", "min_size": 20972032, "max_size": 20972032},
        ],
    },
    "vdi-sparse": {
        "cwd": "vdi-sparse",
        "scripts": ["phase1.script", "phase2.script"],
        "cleanup": ["sparse-test.vdi", "vdi-sparse.log"],
        "title": "sparse VDI allocate-on-write / EXT2 / reopen persistence",
        "verify_file": "vdi-sparse.log",
        "verify_contains": [
            "Created sparse-test.vdi (67108864 bytes) [vdi, sparse]",
            "Initialized GPT on /dev/a",
            "mkfs.ext2: formatting /dev/a1",
            "label=VDISPARSE",
            "vdi-sparse-persistence",
            "Path: /target/etc/vdi-test.txt",
            "Logging to vdi-sparse.log (append)",
        ],
        "verify_host_files": [
            {"path": "sparse-test.vdi", "min_size": 1024, "max_size": 66060288},
        ],
    },
    "vdi-fixed": {
        "cwd": "vdi-fixed",
        "scripts": ["phase1.script", "phase2.script"],
        "cleanup": ["fixed-test.vdi", "vdi-fixed.log"],
        "title": "fixed/full VDI create / partition / EXT2 / reopen persistence",
        "verify_file": "vdi-fixed.log",
        "verify_contains": [
            "Created fixed-test.vdi (20971520 bytes) [vdi]",
            "/dev/a",
            "Initialized GPT on /dev/a",
            "Added linuxfs 'rootfs' at [2048, 40926]",
            "mkfs.ext2: formatting /dev/a1",
            "label=VDIFIXED",
            "vdi-fixed-persistence",
            "Path: /target/etc/vdi-test.txt",
            "Logging to vdi-fixed.log (append)",
        ],
    },
    "vdi-sparse-scale": {
        "cwd": "vdi-sparse-scale",
        "script": "test.script",
        "cleanup": ["scale.vdi", "vdi-sparse-scale.log"],
        "title": "1 GiB sparse VDI map alignment / GPT primary+backup",
        "verify_file": "vdi-sparse-scale.log",
        "verify_contains": [
            "Logical size:      1.00 GiB (1073741824 bytes)",
            "Blocks:            1024",
            "Map invalid:       0",
            "Initialized GPT on /dev/a",
            "Added biosboot 'bios'",
            "Added linuxfs 'rootfs'",
            "/dev/a1",
            "/dev/a2",
            "Container sanity:  PASS",
            "Result: CONTAINER VALID",
        ],
    },
    "vdi-sparse-torture": {
        "cwd": "vdi-sparse-torture",
        "scripts": ["phase1.script", "phase2.script"],
        "cleanup": ["torture.vdi", "phase1.log", "phase2.log"],
        "title": "sparse VDI allocation / zero-fill / boundary / persistence torture",
        "verify_file": "phase2.log",
        "verify_contains": [
            "Variant:           dynamic/sparse",
            "Blocks allocated:  4",
            "Map allocated:     4",
            "Map unallocated:   60",
            "Map invalid:       0",
            "Map duplicates:    0",
            "Container sanity:  PASS",
            "Result: CONTAINER VALID",
        ],
        "verify_script": "verify.py",
        "verify_script_contains": [
            "PASS: cross-block write payload",
            "PASS: unallocated neighboring block reads as zero",
            "PASS: host file growth equals metadata plus allocated blocks",
            "RESULT: PASS (24 checks)",
        ],
    },
    "diskq": {
        "cwd": "diskq",
        "script": "test.script",
        "cleanup": ["fixed-query.vdi", "sparse-query.vdi", "diskq.log"],
        "title": "read-only disk container query / VDI structural audit / GPT layer",
        "verify_file": "diskq.log",
        "verify_contains": [
            "Detected format:   VDI",
            "Variant:           fixed/full",
            "Variant:           dynamic/sparse",
            "Map invalid:       0",
            "Map duplicates:    0",
            "Container sanity:  PASS",
            "Type:              GPT (protective MBR present)",
            "Sanity:            PASS",
            "Blocks allocated:  3",
            "Result: CONTAINER VALID",
        ],
    },
    "disk-output": {
        "cwd": "disk-output",
        "script": "test.script",
        "cleanup": ["disk-output.img", "disk-output.log"],
        "title": "disk/GPT/device application output routing",
        "verify_file": "disk-output.log",
        "verify_contains": [
            "Created disk-output.img (16777216 bytes)",
            "Initialized GPT on /dev/a",
            "Added linuxfs 'rootfs'",
            "Disk: /dev/a  Sector: 512",
            "Idx  Start LBA",
            "linuxfs",
            "/dev/a1",
            "/dev/a     /dev/a",
            "Partition Table: GPT",
            "gpt        gpt <init|add|print>",
            "partscan   Scan GPT and register",
        ],
    },
    "iso": {
        "cwd": "iso-test",
        "scripts": ["iso-test1.script", "test2.script"],
        "cleanup": ["iso-regression.log"],
        "title": "ISO9660 mount / listing + application output routing",
        "verify_file": "iso-regression.log",
        "verify_contains": [
            "/dev/b     /dev/b",
            "/dev/b     iso9660 /            ro",
            "-rw-r--r--",
            "HELLO.TXT",
            "The lazy fox jumped over the hard wood log.",
        ],
    },
    "debian-control": {
        "cwd": "pkg-phase1d",
        "script": "control.script",
        "cleanup": ["pkg-control.log"],
        "title": "real Debian control metadata",
    },
    "debian-state": {
        "cwd": "pkg-phase1e",
        "script": "install-state.script",
        "cleanup": ["pkg-state.img", "pkg-dpkg-state.log"],
        "title": "real Debian package install + dpkg state",
    },
    "debian-dash": {
        "cwd": "pkg-phase1e",
        "script": "dash-state.script",
        "cleanup": ["pkg-dash-state.img", "pkg-dash-state.log"],
        "title": "real Debian dash controlled fresh pre-unpack + dpkg state",
        "verify_file": "pkg-dash-state.log",
        "verify_contains": [
            "pkg pre-unpack: dash: controlled fresh-install preinst",
            "Shell execution: DISABLED",
            "pkg pre-unpack: dash fresh-install preinst has no actions; audited no-op",
            "pkg pre-unpack: dash fresh-install actions complete",
            "Status: install ok unpacked",
            "Path: /target/usr/bin/dash",
            "Path: /target/var/lib/dpkg/info/dash.preinst",
            "Path: /target/var/lib/dpkg/info/dash.postinst",
        ],
    },
    "debian-base-passwd": {
        "cwd": "pkg-phase1e",
        "script": "base-passwd-state.script",
        "cleanup": ["pkg-base-passwd-state.img", "pkg-base-passwd-state.log"],
        "title": "real Debian base-passwd controlled pre-unpack + dpkg state",
        "verify_file": "pkg-base-passwd-state.log",
        "verify_contains": [
            "pkg pre-unpack: base-passwd: controlled fresh-install preinst",
            "pkg pre-unpack: base-passwd created /etc/passwd from package master data",
            "pkg pre-unpack: base-passwd created /etc/group from package master data",
            "pkg pre-unpack: base-passwd fresh-install actions complete",
            "Status: install ok unpacked",
            "root:*:0:0:root:/root:/bin/bash",
            "nogroup:*:65534:",
            "Path: /target/usr/share/base-passwd/passwd.master",
            "Path: /target/usr/share/base-passwd/group.master",
            "Path: /target/var/lib/dpkg/info/base-passwd.preinst",
            "Path: /target/var/lib/dpkg/info/base-passwd.postinst",
        ],
    },
}

DEFAULT = ["app-printf", "pkg", "pkg-scripts", "pkg-script-show", "pkg-preunpack", "pkg-dash-preunpack", "pkg-base-passwd-preunpack", "pkg-base-files-preunpack", "pkg-libc6-configure", "pkg-configure", "pkg-repo-lifecycle", "pkg-repo-gzip", "pkg-plan", "pkg-auto-install", "pkg-audit", "pkg-plan-conflict", "symlink", "file-ops", "ext2-mknod", "rm", "device-registry", "gpt", "ext2-mount", "ext2-inode-groups", "fat-format", "accounts", "metadata", "utility-output", "vmdk-flat", "vmdk-monolithic-flat-scaffold", "vmdk-monolithic-sparse-scaffold", "vmdk-split-flat-scaffold", "vmdk-split-sparse-scaffold", "vhd-fixed", "vhd-ntfs-basic", "ntfs-check", "vdi-fixed", "vdi-sparse", "vdi-sparse-scale", "vdi-sparse-torture", "diskq", "disk-output", "iso"]
DEBIAN = ["debian-control", "debian-state", "debian-base-passwd", "debian-dash"]

def resolve_guppy(value):
    p = Path(value)
    if p.is_absolute():
        return str(p)
    # A path containing a slash is relative to tests/, not each test cwd.
    if "/" in value or "\\" in value:
        return str((HERE / p).resolve())
    # Bare name: let PATH resolve it (normal Windows installed-guppy case).
    found = shutil.which(value)
    return found or value

def cleanup_case(name, case):
    d = HERE / case["cwd"]
    if case.get("reference_fixtures"):
        clean_reference_fixtures(name, d)
    for artifact in case.get("cleanup", []):
        p = d / artifact
        try:
            if p.is_file() or p.is_symlink():
                p.unlink()
            elif p.is_dir():
                shutil.rmtree(p)
        except FileNotFoundError:
            pass

def run_case(name, guppy):
    case = TESTS[name]
    cwd = HERE / case["cwd"]
    cleanup_case(name, case)
    scripts = case.get("scripts", [case.get("script")])
    output_parts = []
    rc = 0

    if case.get("reference_fixtures"):
        try:
            staged = stage_reference_fixtures(name, cwd, guppy, REFERENCE_ISO)
            output_parts.append(f"[REFERENCE] staged {len(staged)} fixture(s) from ISO {REFERENCE_ISO}\n")
        except RuntimeError as exc:
            output_parts.append(f"[REFERENCE] {exc}\n")
            rc = 1

    for script in scripts if rc == 0 else []:
        cmd = [guppy, script]
        try:
            cp = subprocess.run(
                cmd, cwd=cwd, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                errors="replace"
            )
            output_parts.append(cp.stdout or "")
            rc = cp.returncode
        except OSError as e:
            output_parts.append(f"unable to execute {guppy}: {e}\n")
            rc = 127
        if rc != 0:
            break
    output = "".join(output_parts)

    expected_rc = case.get("expected_rc", 0)
    if expected_rc != 0:
        if rc == expected_rc:
            output += f"\n[VERIFY] expected command failure rc={expected_rc} observed\n"
            rc = 0
        else:
            output += f"\n[VERIFY] expected rc={expected_rc}, observed rc={rc}\n"
            if rc == 0:
                rc = 1

    if rc == 0 and case.get("verify_file"):
        verify_path = cwd / case["verify_file"]
        try:
            verify_text = verify_path.read_text(errors="replace")
        except OSError as e:
            output += f"\n[VERIFY] cannot read {verify_path.name}: {e}\n"
            rc = 1
        else:
            missing = [text for text in case.get("verify_contains", []) if text not in verify_text]
            forbidden = [text for text in case.get("verify_not_contains", []) if text in verify_text]
            if missing:
                output += "\n[VERIFY] missing expected logfile text:\n"
                output += "".join(f"  {text}\n" for text in missing)
                rc = 1
            if forbidden:
                output += "\n[VERIFY] unexpected logfile text:\n"
                output += "".join(f"  {text}\n" for text in forbidden)
                rc = 1
            if not missing and not forbidden:
                output += f"\n[VERIFY] {verify_path.name}: expected output present\n"

    if rc == 0 and case.get("verify_script"):
        verify_cmd = [sys.executable, case["verify_script"]]
        try:
            cp = subprocess.run(
                verify_cmd, cwd=cwd, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                errors="replace"
            )
            verify_output = cp.stdout or ""
            output += "\n[VERIFY SCRIPT] " + case["verify_script"] + "\n" + verify_output
            if cp.returncode != 0:
                rc = cp.returncode
            else:
                missing = [
                    text for text in case.get("verify_script_contains", [])
                    if text not in verify_output
                ]
                if missing:
                    output += "\n[VERIFY SCRIPT] missing expected text:\n"
                    output += "".join(f"  {text}\n" for text in missing)
                    rc = 1
        except OSError as e:
            output += f"\n[VERIFY SCRIPT] unable to execute {case['verify_script']}: {e}\n"
            rc = 127

    if rc == 0:
        for check in case.get("verify_host_files", []):
            host_path = cwd / check["path"]
            try:
                size = host_path.stat().st_size
            except OSError as e:
                output += f"\n[VERIFY] cannot stat {check['path']}: {e}\n"
                rc = 1
                continue
            min_size = check.get("min_size")
            max_size = check.get("max_size")
            output += f"\n[VERIFY] {check['path']}: host size={size} bytes\n"
            if min_size is not None and size < min_size:
                output += f"[VERIFY] host file is smaller than {min_size} bytes\n"
                rc = 1
            if max_size is not None and size > max_size:
                output += f"[VERIFY] host file is larger than {max_size} bytes\n"
                rc = 1

    if case.get("reference_fixtures"):
        clean_reference_fixtures(name, cwd)

    LOGDIR.mkdir(exist_ok=True)
    (LOGDIR / f"{name}.log").write_text(output, errors="replace")
    return rc, output, case["title"]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--guppy", default=os.environ.get("GUPPY", "guppy.exe" if os.name == "nt" else "../bin/guppy"))
    ap.add_argument("--suite", choices=["default", "debian"], default="default")
    ap.add_argument("--reference-iso", default=os.environ.get("GUPPY_REFERENCE_ISO"))
    ap.add_argument("tests", nargs="*")
    ns = ap.parse_args()

    names = ns.tests or (DEFAULT if ns.suite == "default" else DEBIAN)
    unknown = [n for n in names if n not in TESTS]
    if unknown:
        print("unknown test(s): " + ", ".join(unknown), file=sys.stderr)
        return 2

    guppy = resolve_guppy(ns.guppy)
    global REFERENCE_ISO
    if ns.reference_iso:
        REFERENCE_ISO = Path(ns.reference_iso).resolve()
    else:
        REFERENCE_ISO = (HERE / "media" / "guppy-reference.iso").resolve()
    LOGDIR.mkdir(exist_ok=True)
    aggregate = LOGDIR / ("regression.log" if ns.suite == "default" else "debian-regression.log")

    chunks = []
    results = []
    header = [
        "=" * 68,
        "GUPPY REGRESSION TEST",
        f"Suite: {ns.suite}",
        f"Started: {datetime.now().isoformat(timespec='seconds')}",
        f"Guppy: {guppy}",
        "=" * 68,
        "",
    ]
    chunks.extend(header)

    failed = 0
    for name in names:
        rc, output, title = run_case(name, guppy)
        status = "PASS" if rc == 0 else f"FAIL (rc={rc})"
        if rc != 0:
            failed += 1
        print(f"[TEST] {name:<16} {status}")
        chunks += [
            f"[TEST] {name} - {title}",
            "-" * 68,
            output.rstrip(),
            f"[RESULT] {status}",
            "",
        ]
        results.append((name, status))

    chunks += ["=" * 68, "SUMMARY"]
    for name, status in results:
        chunks.append(f"{name:<20} {status}")
    chunks += [
        "",
        f"{len(results)-failed} passed, {failed} failed",
        "=" * 68,
        "",
    ]
    aggregate.write_text("\n".join(chunks), errors="replace")
    print(f"Audit log: {aggregate}")
    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
