pkg maintainer-script scan regression
====================================
Read-only scanner fixture. It deliberately contains filesystem, ldconfig, alternatives, initramfs, systemd, dpkg-trigger, and /proc references. The command inventories signals and NEVER executes the script.
