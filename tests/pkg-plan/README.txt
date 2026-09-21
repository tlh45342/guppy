pkg repository/planner regression

Builds a small EXT2 target and a synthetic Debian repository index under
/target/repo/dists/test/main/binary-amd64/Packages, then verifies:

- pkg repo add / pkg repo list
- package lookup through Filename metadata
- installed package visibility from /var/lib/dpkg/status
- read-only dependency closure planning
- already-unpacked package recognition
- strongly-connected dependency cycle reporting

The synthetic graph intentionally contains the same cycle shapes observed
while building DEBMIN3:

  libc6 <-> libgcc-s1
  libssl3t64 <-> openssl-provider-legacy
- deterministic dependency-before-dependent unpack planning
- SCC cycles emitted as atomic unpack groups
- already-present packages marked without being scheduled for unpack

