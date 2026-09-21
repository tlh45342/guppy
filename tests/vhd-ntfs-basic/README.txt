Fixed VHD + GPT + NTFS probe regression
========================================

Creates a 1 GiB legacy fixed VHD, initializes GPT, creates a Microsoft Basic
Data partition at 1 MiB alignment, then runs Guppy's current minimal/probe NTFS
formatter on that partition.  A second Guppy process reopens the image and
verifies that the VHD and GPT remain valid.

Important: Guppy's mkfs.ntfs is not yet a complete Windows-mountable NTFS
formatter.  The test validates container/partition routing and the current NTFS
boot/MFT stub placement; it does not claim full NTFS interoperability yet.

The resulting fixed-ntfs.vhd is retained after the test so it can be inspected
externally.  The regression runner removes the prior image at the beginning of
the next run of this case.
