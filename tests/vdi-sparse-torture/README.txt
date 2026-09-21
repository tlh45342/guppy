Sparse VDI torture regression

This test is intentionally lower-level than the ordinary vdi-sparse filesystem
regression. It attacks allocation-on-write behavior directly.

It checks:
- untouched sparse guest sectors read as zero;
- scattered writes allocate nonadjacent 1 MiB blocks;
- partial writes do not contaminate unwritten bytes;
- a single write crossing a 1 MiB guest block boundary survives correctly;
- neighboring guest blocks remain unchanged;
- allocation-map state survives close/reopen;
- diskq reports no invalid or duplicate VDI block mappings;
- host file growth tracks allocations rather than logical capacity.

The image is never mounted by the host OS.
