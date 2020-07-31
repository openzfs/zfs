SUBDIRS = bash_completion.d pyzfs zcp
if BUILD_LINUX
SUBDIRS += bpftrace dracut initramfs
endif
if PAM_ZFS_ENABLED
SUBDIRS += pam_zfs_key
endif
DIST_SUBDIRS = bash_completion.d bpftrace dracut initramfs pam_zfs_key pyzfs zcp
