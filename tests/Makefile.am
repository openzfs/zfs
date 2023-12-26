include $(srcdir)/%D%/zfs-tests/Makefile.am


scripts_test_runner_bindir = $(datadir)/$(PACKAGE)/test-runner/bin
scripts_test_runner_bin_SCRIPTS = \
	%D%/test-runner/bin/test-runner.py \
	%D%/test-runner/bin/zts-report.py

SUBSTFILES += $(scripts_test_runner_bin_SCRIPTS)


scripts_test_runner_includedir = $(datadir)/$(PACKAGE)/test-runner/include
dist_scripts_test_runner_include_DATA = \
	%D%/test-runner/include/logapi.shlib


scripts_runfilesdir = $(datadir)/$(PACKAGE)/runfiles
dist_scripts_runfiles_DATA = \
	%D%/runfiles/bclone.run \
	%D%/runfiles/common.run \
	%D%/runfiles/freebsd.run \
	%D%/runfiles/linux.run \
	%D%/runfiles/longevity.run \
	%D%/runfiles/perf-regression.run \
	%D%/runfiles/sanity.run \
	%D%/runfiles/sunos.run


dist_noinst_DATA += %D%/README.md

SHELLCHECKSCRIPTS += $(shell find $(srcdir)/%D% -name '*.sh')
