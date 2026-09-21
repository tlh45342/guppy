Independent NTFS format structural checker
==========================================

This regression intentionally checks Guppy's mkfs.ntfs output with code that
is separate from the formatter implementation.  It does not repair anything
and it is not intended to replace Windows chkdsk.

The checker currently validates the subset of NTFS that Guppy emits:

  * GPT Microsoft Basic Data partition discovery
  * primary/backup NTFS boot-sector geometry and signatures
  * $MFT and $MFTMirr locations
  * FILE record headers and Update Sequence Array (USA/fixup) integrity
  * MFT metadata records 0 through 8
  * resident $STANDARD_INFORMATION and $FILE_NAME attributes
  * root resident $INDEX_ROOT namespace from Pass 4
  * nonresident $DATA runlists for $MFT, $MFTMirr, $Bitmap, $LogFile,
    $AttrDef, and $Boot
  * $Bitmap allocation consistency for all emitted nonresident runs
  * $Volume name and clean NTFS 3.1 $VOLUME_INFORMATION
  * a compact $AttrDef table and its type mappings
  * reserved/zeroed $LogFile storage for this structural stage
  * $Boot mapping to the first two volume clusters and primary boot sector
  * an empty $BadClus $DATA seed
  * first four $MFT records mirrored by $MFTMirr
  * attribute IDs, bounds, alignment, and END marker placement

A negative self-test copies the image, corrupts an MFT sector-tail fixup, and
requires the checker to reject the corrupted image.  This proves that a PASS is
not merely checking magic strings.

Pass 5 is still an intermediate formatter milestone.  The $LogFile reservation
is deliberately structural rather than a complete NTFS restart/log stream, and
$BadClus is only the initial empty-data seed.  The resident root index also
remains the Pass 4 six-entry set; adding the remaining system namespace cleanly
will require the later index-allocation/modern-metadata work.

Windows mount/chkdsk remains the external interoperability oracle.  A checker
PASS means Guppy's emitted structures agree with this independently parsed
milestone; it does not claim that Windows must mount the volume yet.
