Round 13 libc6/libc-bin controlled configuration + native ldconfig regression.

Proves:
- libc6 controlled fresh configure queues libc-upgrade and ldconfig
- libc-bin interest-await ldconfig is registered persistently
- libc-bin fresh configure copies default nsswitch.conf only when absent
- Guppy builds a native glibc 1.1 /etc/ld.so.cache without host ldconfig
- ldconfig trigger dispatch runs libc-bin's controlled triggered action
- successful ldconfig processing retires only ldconfig from Unincorp
- unsupported libc-upgrade remains pending rather than being faked or dropped
