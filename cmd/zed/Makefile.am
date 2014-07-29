include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

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
	$(top_builddir)/lib/libzfs/libzfs.la \
	$(top_builddir)/lib/libzfs_core/libzfs_core.la

zedconfdir = $(sysconfdir)/zfs/zed.d

dist_zedconf_DATA = \
	$(top_srcdir)/cmd/zed/zed.d/zed.rc

zedexecdir = $(libexecdir)/zfs/zed.d

dist_zedexec_SCRIPTS = \
	$(top_srcdir)/cmd/zed/zed.d/all-debug.sh \
	$(top_srcdir)/cmd/zed/zed.d/all-syslog.sh \
	$(top_srcdir)/cmd/zed/zed.d/checksum-email.sh \
	$(top_srcdir)/cmd/zed/zed.d/checksum-spare.sh \
	$(top_srcdir)/cmd/zed/zed.d/data-email.sh \
	$(top_srcdir)/cmd/zed/zed.d/generic-email.sh \
	$(top_srcdir)/cmd/zed/zed.d/io-email.sh \
	$(top_srcdir)/cmd/zed/zed.d/io-spare.sh \
	$(top_srcdir)/cmd/zed/zed.d/resilver.finish-email.sh \
	$(top_srcdir)/cmd/zed/zed.d/scrub.finish-email.sh

zedconfdefaults = \
	all-syslog.sh \
	checksum-email.sh \
	checksum-spare.sh \
	data-email.sh \
	io-email.sh \
	io-spare.sh \
	resilver.finish-email.sh \
	scrub.finish-email.sh

install-data-local:
	$(MKDIR_P) "$(DESTDIR)$(zedconfdir)"
	for f in $(zedconfdefaults); do \
	  test -f "$(DESTDIR)$(zedconfdir)/$${f}" -o \
	       -L "$(DESTDIR)$(zedconfdir)/$${f}" || \
	    ln -s "$(zedexecdir)/$${f}" "$(DESTDIR)$(zedconfdir)"; \
	done
