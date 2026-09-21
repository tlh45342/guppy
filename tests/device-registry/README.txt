device-registry regression
==========================

Proves that replacing media behind an existing device name does not leave
partition children from the previous image in the virtual block registry.

The setup first creates /dev/a1 on a GPT image, then reattaches /dev/a to a
blank image. The logged phase verifies that `use`, `partscan --verify`, and an
actual `partscan` all see only the new whole-disk device and never the stale
/dev/a1 child.
