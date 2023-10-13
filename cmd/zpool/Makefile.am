zpool_CFLAGS  = $(AM_CFLAGS)
zpool_CFLAGS += $(LIBBLKID_CFLAGS) $(LIBUUID_CFLAGS)

zpool_CPPFLAGS  = $(AM_CPPFLAGS)
zpool_CPPFLAGS += -I$(srcdir)/%D%

sbin_PROGRAMS   += zpool
CPPCHECKTARGETS += zpool

zpool_SOURCES = \
	%D%/zpool_iter.c \
	%D%/zpool_main.c \
	%D%/zpool_util.c \
	%D%/zpool_util.h \
	%D%/zpool_vdev.c

if BUILD_FREEBSD
zpool_SOURCES += \
	%D%/os/freebsd/zpool_vdev_os.c
endif

if BUILD_LINUX
zpool_SOURCES += \
	%D%/os/linux/zpool_vdev_os.c
endif

zpool_LDADD = \
	libzfs.la \
	libzfs_core.la \
	libnvpair.la \
	libuutil.la \
	libzutil.la

zpool_LDADD += $(LTLIBINTL)

if BUILD_FREEBSD
zpool_LDADD += -lgeom
endif
zpool_LDADD += -lm $(LIBBLKID_LIBS) $(LIBUUID_LIBS)

dist_noinst_DATA += %D%/zpool.d/README

SHELLCHECKSCRIPTS += $(dist_zpoolexec_SCRIPTS)
zpoolexecdir = $(zfsexecdir)/zpool.d
dist_zpoolexec_SCRIPTS = \
	%D%/zpool.d/ata_err \
	%D%/zpool.d/cmd_to \
	%D%/zpool.d/defect \
	%D%/zpool.d/dm-deps \
	%D%/zpool.d/enc \
	%D%/zpool.d/encdev \
	%D%/zpool.d/fault_led \
	%D%/zpool.d/health \
	%D%/zpool.d/hours_on \
	%D%/zpool.d/iostat \
	%D%/zpool.d/iostat-10s \
	%D%/zpool.d/iostat-1s \
	%D%/zpool.d/label \
	%D%/zpool.d/locate_led \
	%D%/zpool.d/lsblk \
	%D%/zpool.d/media \
	%D%/zpool.d/model \
	%D%/zpool.d/nonmed \
	%D%/zpool.d/nvme_err \
	%D%/zpool.d/off_ucor \
	%D%/zpool.d/pend_sec \
	%D%/zpool.d/pwr_cyc \
	%D%/zpool.d/r_proc \
	%D%/zpool.d/r_ucor \
	%D%/zpool.d/realloc \
	%D%/zpool.d/rep_ucor \
	%D%/zpool.d/serial \
	%D%/zpool.d/ses \
	%D%/zpool.d/size \
	%D%/zpool.d/slot \
	%D%/zpool.d/smart \
	%D%/zpool.d/smart_test \
	%D%/zpool.d/smartx \
	%D%/zpool.d/temp \
	%D%/zpool.d/test_ended \
	%D%/zpool.d/test_progress \
	%D%/zpool.d/test_status \
	%D%/zpool.d/test_type \
	%D%/zpool.d/upath \
	%D%/zpool.d/vendor \
	%D%/zpool.d/w_proc \
	%D%/zpool.d/w_ucor

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
	%D%/compatibility.d/compat-2018 \
	%D%/compatibility.d/compat-2019 \
	%D%/compatibility.d/compat-2020 \
	%D%/compatibility.d/compat-2021 \
	%D%/compatibility.d/freebsd-11.0 \
	%D%/compatibility.d/freebsd-11.2 \
	%D%/compatibility.d/freebsd-11.3 \
	%D%/compatibility.d/freenas-9.10.2 \
	%D%/compatibility.d/grub2 \
	%D%/compatibility.d/openzfs-2.0-freebsd \
	%D%/compatibility.d/openzfs-2.0-linux \
	%D%/compatibility.d/openzfs-2.1-freebsd \
	%D%/compatibility.d/openzfs-2.1-linux \
	%D%/compatibility.d/openzfs-2.2 \
	%D%/compatibility.d/openzfsonosx-1.7.0 \
	%D%/compatibility.d/openzfsonosx-1.8.1 \
	%D%/compatibility.d/openzfsonosx-1.9.3 \
	%D%/compatibility.d/zol-0.6.1 \
	%D%/compatibility.d/zol-0.6.4 \
	%D%/compatibility.d/zol-0.6.5 \
	%D%/compatibility.d/zol-0.7 \
	%D%/compatibility.d/zol-0.8

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
	"freebsd-11.3		freebsd-12.3" \
	"freebsd-11.3		freebsd-12.4" \
	"openzfs-2.1-freebsd	freebsd-13.0" \
	"openzfs-2.1-freebsd	freebsd-13.1" \
	"openzfs-2.1-freebsd	freebsd-13.2" \
	"freebsd-11.3		freenas-11.3" \
	"freenas-11.0		freenas-11.1" \
	"openzfsonosx-1.9.3	openzfsonosx-1.9.4" \
	"openzfs-2.0-freebsd	truenas-12.0" \
	"zol-0.7		ubuntu-18.04" \
	"zol-0.8		ubuntu-20.04" \
	"openzfs-2.1-linux	ubuntu-22.04" \
	"openzfs-2.2		openzfs-2.2-linux" \
	"openzfs-2.2		openzfs-2.2-freebsd"

zpoolconfdir = $(sysconfdir)/zfs/zpool.d
INSTALL_DATA_HOOKS += zpool-install-data-hook
zpool-install-data-hook:
	$(MKDIR_P) "$(DESTDIR)$(zpoolconfdir)"
	set -x; for f in $(zpoolconfdefaults); do \
	  [ -f "$(DESTDIR)$(zpoolconfdir)/$${f}" ] || \
	  [ -L "$(DESTDIR)$(zpoolconfdir)/$${f}" ] || \
	    $(LN_S) "$(zpoolexecdir)/$${f}" "$(DESTDIR)$(zpoolconfdir)"; \
	done
	set -x; printf '%s\n' $(zpoolcompatlinks) | \
	while read -r canon alias; do \
		$(LN_S) -f "$${canon}" "$(DESTDIR)$(zpoolcompatdir)/$${alias}"; \
	done
