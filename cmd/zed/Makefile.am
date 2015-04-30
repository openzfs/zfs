include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

EXTRA_DIST = $(top_srcdir)/cmd/zed/zed.d/README

sbin_PROGRAMS = zed

zed_SOURCES = \
	$(top_srcdir)/cmd/zed/zed.c \
	$(top_srcdir)/cmd/zed/zed.h \
	$(top_srcdir)/cmd/zed/zed_conf.c \
	$(top_srcdir)/cmd/zed/zed_conf.h \
	$(top_srcdir)/cmd/zed/zed_event.c \
	$(top_srcdir)/cmd/zed/zed_event.h \
	$(top_srcdir)/cmd/zed/zed_exec.c \
	$(top_srcdir)/cmd/zed/zed_exec.h \
	$(top_srcdir)/cmd/zed/zed_file.c \
	$(top_srcdir)/cmd/zed/zed_file.h \
	$(top_srcdir)/cmd/zed/zed_log.c \
	$(top_srcdir)/cmd/zed/zed_log.h \
	$(top_srcdir)/cmd/zed/zed_strings.c \
	$(top_srcdir)/cmd/zed/zed_strings.h

zed_LDADD = \
	$(top_builddir)/lib/libavl/libavl.la \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libspl/libspl.la \
	$(top_builddir)/lib/libzpool/libzpool.la \
	$(top_builddir)/lib/libzfs/libzfs.la \
	$(top_builddir)/lib/libzfs_core/libzfs_core.la

zedconfdir = $(sysconfdir)/zfs/zed.d

dist_zedconf_DATA = \
	$(top_srcdir)/cmd/zed/zed.d/zed-functions.sh \
	$(top_srcdir)/cmd/zed/zed.d/zed.rc

zedexecdir = $(libexecdir)/zfs/zed.d

dist_zedexec_SCRIPTS = \
	$(top_srcdir)/cmd/zed/zed.d/all-debug.sh \
	$(top_srcdir)/cmd/zed/zed.d/all-syslog.sh \
	$(top_srcdir)/cmd/zed/zed.d/checksum-notify.sh \
	$(top_srcdir)/cmd/zed/zed.d/checksum-spare.sh \
	$(top_srcdir)/cmd/zed/zed.d/data-notify.sh \
	$(top_srcdir)/cmd/zed/zed.d/generic-notify.sh \
	$(top_srcdir)/cmd/zed/zed.d/io-notify.sh \
	$(top_srcdir)/cmd/zed/zed.d/io-spare.sh \
	$(top_srcdir)/cmd/zed/zed.d/resilver.finish-notify.sh \
	$(top_srcdir)/cmd/zed/zed.d/scrub.finish-notify.sh

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
