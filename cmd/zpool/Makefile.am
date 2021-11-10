include $(top_srcdir)/config/Rules.am
include $(top_srcdir)/config/Shellcheck.am

AM_CFLAGS += $(LIBBLKID_CFLAGS) $(LIBUUID_CFLAGS)

DEFAULT_INCLUDES += -I$(srcdir)

sbin_PROGRAMS = zpool

zpool_SOURCES = \
	zpool_iter.c \
	zpool_main.c \
	zpool_util.c \
	zpool_util.h \
	zpool_vdev.c

if BUILD_FREEBSD
zpool_SOURCES += os/freebsd/zpool_vdev_os.c
endif

if BUILD_LINUX
zpool_SOURCES += os/linux/zpool_vdev_os.c
endif

zpool_LDADD = \
	$(abs_top_builddir)/lib/libzfs/libzfs.la \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la \
	$(abs_top_builddir)/lib/libuutil/libuutil.la \
	$(abs_top_builddir)/lib/libzutil/libzutil.la

zpool_LDADD += $(LTLIBINTL)

if BUILD_FREEBSD
zpool_LDADD += -lgeom
endif
zpool_LDADD += -lm $(LIBBLKID_LIBS) $(LIBUUID_LIBS)

include $(top_srcdir)/config/CppCheck.am

zpoolconfdir = $(sysconfdir)/zfs/zpool.d
zpoolexecdir = $(zfsexecdir)/zpool.d

EXTRA_DIST = zpool.d/README compatibility.d

dist_zpoolexec_SCRIPTS = \
	zpool.d/dm-deps \
	zpool.d/enc \
	zpool.d/encdev \
	zpool.d/fault_led \
	zpool.d/iostat \
	zpool.d/iostat-1s \
	zpool.d/iostat-10s \
	zpool.d/label \
	zpool.d/locate_led \
	zpool.d/lsblk \
	zpool.d/media \
	zpool.d/model \
	zpool.d/serial \
	zpool.d/ses \
	zpool.d/size \
	zpool.d/slot \
	zpool.d/smart \
	zpool.d/smartx \
	zpool.d/temp \
	zpool.d/health \
	zpool.d/r_proc \
	zpool.d/w_proc \
	zpool.d/r_ucor \
	zpool.d/w_ucor \
	zpool.d/nonmed \
	zpool.d/defect \
	zpool.d/hours_on \
	zpool.d/realloc \
	zpool.d/rep_ucor \
	zpool.d/cmd_to \
	zpool.d/pend_sec \
	zpool.d/off_ucor \
	zpool.d/ata_err \
	zpool.d/nvme_err \
	zpool.d/pwr_cyc \
	zpool.d/upath \
	zpool.d/vendor \
	zpool.d/smart_test \
	zpool.d/test_type \
	zpool.d/test_status \
	zpool.d/test_progress \
	zpool.d/test_ended

zpoolconfdefaults = \
	dm-deps \
	enc \
	encdev \
	fault_led \
	iostat \
	iostat-1s \
	iostat-10s \
	label \
	locate_led \
	lsblk \
	media \
	model \
	serial \
	ses \
	size \
	slot \
	smart \
	smartx \
	temp \
	health \
	r_proc \
	w_proc \
	r_ucor \
	w_ucor \
	nonmed \
	defect \
	hours_on \
	realloc \
	rep_ucor \
	cmd_to \
	pend_sec \
	off_ucor \
	ata_err \
	nvme_err \
	pwr_cyc \
	upath \
	vendor \
	smart_test \
	test_type \
	test_status \
	test_progress \
	test_ended

zpoolcompatdir = $(pkgdatadir)/compatibility.d

dist_zpoolcompat_DATA = \
	compatibility.d/compat-2018 \
	compatibility.d/compat-2019 \
	compatibility.d/compat-2020 \
	compatibility.d/compat-2021 \
	compatibility.d/freebsd-11.0 \
	compatibility.d/freebsd-11.2 \
	compatibility.d/freebsd-11.3 \
	compatibility.d/freenas-9.10.2 \
	compatibility.d/grub2 \
	compatibility.d/openzfsonosx-1.7.0 \
	compatibility.d/openzfsonosx-1.8.1 \
	compatibility.d/openzfsonosx-1.9.3 \
	compatibility.d/openzfs-2.0-freebsd \
	compatibility.d/openzfs-2.0-linux \
	compatibility.d/openzfs-2.1-freebsd \
	compatibility.d/openzfs-2.1-linux \
	compatibility.d/zol-0.6.1 \
	compatibility.d/zol-0.6.4 \
	compatibility.d/zol-0.6.5 \
	compatibility.d/zol-0.7 \
	compatibility.d/zol-0.8

# canonical <- alias symbolic link pairs
# eg: "2018" is a link to "compat-2018"
zpoolcompatlinks = \
	"compat-2018		2018" \
	"compat-2019		2019" \
	"compat-2020		2020" \
	"compat-2021		2021" \
	"freebsd-11.0		freebsd-11.1" \
	"freebsd-11.0		freenas-11.0" \
	"freebsd-11.2		freenas-11.2" \
	"freebsd-11.3		freebsd-11.4" \
	"freebsd-11.3		freebsd-12.0" \
	"freebsd-11.3		freebsd-12.1" \
	"freebsd-11.3		freebsd-12.2" \
	"freebsd-11.3		freenas-11.3" \
	"freenas-11.0		freenas-11.1" \
	"openzfsonosx-1.9.3	openzfsonosx-1.9.4" \
	"openzfs-2.0-freebsd	truenas-12.0" \
	"zol-0.7		ubuntu-18.04" \
	"zol-0.8		ubuntu-20.04"

install-data-hook:
	$(MKDIR_P) "$(DESTDIR)$(zpoolconfdir)"
	for f in $(zpoolconfdefaults); do \
	  test -f "$(DESTDIR)$(zpoolconfdir)/$${f}" -o \
	       -L "$(DESTDIR)$(zpoolconfdir)/$${f}" || \
	    ln -s "$(zpoolexecdir)/$${f}" "$(DESTDIR)$(zpoolconfdir)"; \
	done
	for l in $(zpoolcompatlinks); do \
		(cd "$(DESTDIR)$(zpoolcompatdir)"; ln -sf $${l} ); \
	done
