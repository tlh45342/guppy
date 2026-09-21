Sparse VDI scale/alignment regression

A 1 GiB VDI has 1024 1-MiB guest blocks, therefore a 4096-byte block map.
That map exactly fills the metadata span from 0x200 through 0x11ff.

This regression ensures:
- a freshly created 1 GiB sparse VDI has 1024 unallocated and zero invalid map entries;
- GPT primary and backup structures can be written;
- the 1 MiB BIOS partition and remaining root partition can be added;
- partscan finds both partitions;
- diskq still reports a structurally valid sparse VDI.
