GPT regression
==============

Exercises GPT initialization, both partition-add syntaxes, adjacent partition
ranges, registry geometry, and rereading an existing GPT after reattaching the
same image.

The test also attempts to add an overlapping third partition. The GPT writer
must reject it: the final GPT and device registry must contain only /dev/a1 and
/dev/a2, with the original non-overlapping geometry.
