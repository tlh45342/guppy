VMDK BIOS boot / LBA-0 interoperability test
============================================

Purpose
-------
This test is intentionally simpler than vmdk-flat.  It does not create GPT or
EXT2.  Instead it proves the exact path that matters for firmware boot:

    VMware descriptor -> external -flat.vmdk extent -> virtual LBA 0

boot.asm is assembled with the project x86-as assembler.  The resulting
512-byte sector prints:

    HELLO FROM GUPPY VMDK
    VIRTUAL LBA 0 IS GOOD

and halts.  The independent verifier confirms that boot.bin is exactly the
first 512 bytes of the flat extent and that bytes 510-511 are 55 AA.

Run
---
From this directory:

    make

Or from the Guppy repository root:

    make test-vmdk-boot

x86-as must be installed/in PATH.  This test is intentionally NOT part of the
normal Guppy default regression suite because x86-as is an external project
toolchain dependency.

External interoperability check
-------------------------------
The generated files are:

    boot-test.vmdk
    boot-test-flat.vmdk

Copy/attach BOTH files, but attach/select boot-test.vmdk as the virtual disk.
Use legacy BIOS firmware, not UEFI, and make the disk bootable/first in boot
order.  A successful VirtualBox or ESXi boot displays the two lines above.

Cleanup
-------

    make clean

removes boot.bin, the descriptor, flat extent, and test log.
