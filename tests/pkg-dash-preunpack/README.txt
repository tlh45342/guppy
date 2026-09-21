Synthetic regression for the audited Debian dash 0.5.12-12 fresh-install preinst.
The real preinst only performs substantive work for upgrades from versions older
than 0.5.12-9~. Guppy therefore recognizes the audited signature and treats the
fresh-install path as a controlled no-op. Shell execution remains disabled.
