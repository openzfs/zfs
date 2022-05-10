
refpoldir = $(datadir)/selinux/packages
refpol_DATA = pam_zfs_key.pp

EXTRA_DIST = pam_zfs_key.te pam_zfs_key.fc

refpolifdir = $(datadir)/selinux/devel/include/contrib
dist_refpolif_DATA = pam_zfs_key.if

%.pp: %.te %.fc %.if
	$(MAKE) -f $(selinux_makefile) $@

clean-local:
	$(MAKE) -f $(selinux_makefile) clean

# Avoid race condition with tmp/all_interfaces.conf
.NOTPARALLEL: %.pp
