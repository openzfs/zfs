include $(top_srcdir)/config/Shellcheck.am

SUBDIRS = runfiles test-runner zfs-tests

EXTRA_DIST = README.md

SHELLCHECK_IGNORE = ,SC2155
SHELLCHECKSCRIPTS = $$(find . -name '*.sh')
.PHONY: $(SHELLCHECKSCRIPTS)
