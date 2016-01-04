include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

EXTRA_DIST = zed.d/README

sbin_PROGRAMS = zed

zed_SOURCES = \
	zed.c \
	zed.h \
	zed_conf.c \
	zed_conf.h \
	zed_event.c \
	zed_event.h \
	zed_exec.c \
	zed_exec.h \
	zed_file.c \
	zed_file.h \
	zed_log.c \
	zed_log.h \
	zed_strings.c \
	zed_strings.h

zed_LDADD = \
	$(top_builddir)/lib/libavl/libavl.la \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libspl/libspl.la \
	$(top_builddir)/lib/libzpool/libzpool.la \
	$(top_builddir)/lib/libzfs/libzfs.la \
	$(top_builddir)/lib/libzfs_core/libzfs_core.la

zedconfdir = $(sysconfdir)/zfs/zed.d

dist_zedconf_DATA = \
	zed.d/zed-functions.sh \
	zed.d/zed.rc

zedexecdir = $(libexecdir)/zfs/zed.d

dist_zedexec_SCRIPTS = \
	zed.d/all-debug.sh \
	zed.d/all-syslog.sh \
	zed.d/checksum-notify.sh \
	zed.d/checksum-spare.sh \
	zed.d/data-notify.sh \
	zed.d/generic-notify.sh \
	zed.d/io-notify.sh \
	zed.d/io-spare.sh \
	zed.d/resilver.finish-notify.sh \
	zed.d/scrub.finish-notify.sh

zedconfdefaults = \
	all-syslog.sh \
	checksum-notify.sh \
	checksum-spare.sh \
	data-notify.sh \
	io-notify.sh \
	io-spare.sh \
	resilver.finish-notify.sh \
	scrub.finish-notify.sh

install-data-hook:
	$(MKDIR_P) "$(DESTDIR)$(zedconfdir)"
	for f in $(zedconfdefaults); do \
	  test -f "$(DESTDIR)$(zedconfdir)/$${f}" -o \
	       -L "$(DESTDIR)$(zedconfdir)/$${f}" || \
	    ln -s "$(zedexecdir)/$${f}" "$(DESTDIR)$(zedconfdir)"; \
	done
	chmod 0600 "$(DESTDIR)$(zedconfdir)/zed.rc"
