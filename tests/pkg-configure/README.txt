Round 10 controlled package configuration regression.

Proves:
- fresh-install configuration remains shell-free
- previous-version-only postinst branch is skipped
- tmpfiles.d type 'd' creates directory with requested mode/owner
- activate-noawait trigger control queues canonical Unincorp entry
- package status advances unpacked -> installed only after all actions succeed
