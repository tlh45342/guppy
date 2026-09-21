diskq regression

Exercises the read-only disk query path against both fixed/full and
dynamic/sparse VDI images.

The test proves:
- format detection comes from container bytes, not only filename extension;
- fixed and sparse VDI metadata can be reported;
- VDI block-map allocation counts are audited;
- invalid and duplicate map entries are absent;
- a GPT inside the logical VDI is detected as a separate partition layer;
- sparse allocation remains visible after EXT2 formatting.

diskq is read-only by design.
