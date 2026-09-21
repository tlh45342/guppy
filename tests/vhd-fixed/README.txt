Fixed VHD regression
====================

Creates a legacy fixed VHD, writes GPT + EXT2 through libvdisk, closes the
process, reopens the VHD, and verifies data persistence. diskq validates the
VHD footer and then inspects the GPT through logical block access.
