PHONY =
include $(top_srcdir)/config/Shellcheck.am

SUBDIRS = runfiles test-runner zfs-tests

EXTRA_DIST = README.md

SHELLCHECKSCRIPTS += $(shell find $(srcdir) -name '*.sh')
