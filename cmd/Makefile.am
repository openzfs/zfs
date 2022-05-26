bin_SCRIPTS =
bin_PROGRAMS =
sbin_SCRIPTS =
sbin_PROGRAMS =
dist_bin_SCRIPTS =
zfsexec_PROGRAMS =
mounthelper_PROGRAMS =


sbin_SCRIPTS      += fsck.zfs
SHELLCHECKSCRIPTS += fsck.zfs
CLEANFILES        += fsck.zfs
dist_noinst_DATA  += %D%/fsck.zfs.in
$(call SUBST,fsck.zfs,%D%/)


sbin_PROGRAMS   += zfs_ids_to_path
CPPCHECKTARGETS += zfs_ids_to_path

zfs_ids_to_path_SOURCES = \
	%D%/zfs_ids_to_path.c

zfs_ids_to_path_LDADD = \
	libzfs.la


zhack_CPPFLAGS = $(AM_CPPFLAGS) $(FORCEDEBUG_CPPFLAGS)

sbin_PROGRAMS   += zhack
CPPCHECKTARGETS += zhack

zhack_SOURCES = \
	%D%/zhack.c

zhack_LDADD = \
	libzpool.la \
	libzfs_core.la \
	libnvpair.la


ztest_CFLAGS    = $(AM_CFLAGS) $(KERNEL_CFLAGS)
# Get rid of compiler warning for unchecked truncating snprintfs on gcc 7.1.1
ztest_CFLAGS   += $(NO_FORMAT_TRUNCATION)
ztest_CPPFLAGS  = $(AM_CPPFLAGS) $(FORCEDEBUG_CPPFLAGS)

sbin_PROGRAMS   += ztest
CPPCHECKTARGETS += ztest

ztest_SOURCES = \
	%D%/ztest.c

ztest_LDADD = \
	libzpool.la \
	libzfs_core.la \
	libnvpair.la

ztest_LDADD += -lm
ztest_LDFLAGS = -pthread


include $(srcdir)/%D%/raidz_test/Makefile.am
include $(srcdir)/%D%/zdb/Makefile.am
include $(srcdir)/%D%/zfs/Makefile.am
include $(srcdir)/%D%/zinject/Makefile.am
include $(srcdir)/%D%/zpool/Makefile.am
include $(srcdir)/%D%/zpool_influxdb/Makefile.am
include $(srcdir)/%D%/zstream/Makefile.am


if BUILD_LINUX
mounthelper_PROGRAMS += mount.zfs
CPPCHECKTARGETS      += mount.zfs

mount_zfs_SOURCES = \
	%D%/mount_zfs.c

mount_zfs_LDADD = \
	libzfs.la \
	libzfs_core.la \
	libnvpair.la

mount_zfs_LDADD += $(LTLIBINTL)

CPPCHECKTARGETS += raidz_test


sbin_PROGRAMS   += zgenhostid
CPPCHECKTARGETS += zgenhostid

zgenhostid_SOURCES = \
	%D%/zgenhostid.c


dist_bin_SCRIPTS  += %D%/zvol_wait
SHELLCHECKSCRIPTS += %D%/zvol_wait


include $(srcdir)/%D%/zed/Makefile.am
endif


if USING_PYTHON
bin_SCRIPTS      += arc_summary     arcstat        dbufstat
CLEANFILES       += arc_summary     arcstat        dbufstat
dist_noinst_DATA += %D%/arc_summary %D%/arcstat.in %D%/dbufstat.in

$(call SUBST,arcstat,%D%/)
$(call SUBST,dbufstat,%D%/)
arc_summary: %D%/arc_summary
	$(AM_V_at)cp $< $@
endif


PHONY += cmd
cmd: $(bin_SCRIPTS) $(bin_PROGRAMS) $(sbin_SCRIPTS) $(sbin_PROGRAMS) $(dist_bin_SCRIPTS) $(zfsexec_PROGRAMS) $(mounthelper_PROGRAMS)
