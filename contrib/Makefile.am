include $(srcdir)/%D%/bash_completion.d/Makefile.am
include $(srcdir)/%D%/pyzfs/Makefile.am
include $(srcdir)/%D%/zcp/Makefile.am

if BUILD_LINUX
include $(srcdir)/%D%/bpftrace/Makefile.am
include $(srcdir)/%D%/dracut/Makefile.am
include $(srcdir)/%D%/initramfs/Makefile.am
endif

if PAM_ZFS_ENABLED
include $(srcdir)/%D%/pam_zfs_key/Makefile.am
endif
