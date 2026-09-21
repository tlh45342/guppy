VMDK flat regression
====================

Exercises the first VMware-oriented VMDK backend: a small text descriptor plus
one external, fully allocated -flat.vmdk extent.  The test writes GPT + EXT2,
closes Guppy, reopens the descriptor, and verifies persistence.

This is deliberately the ESXi-oriented starting point.  Sparse and split VMDK
layouts are named by the CLI scaffold but are not implemented in this round.
