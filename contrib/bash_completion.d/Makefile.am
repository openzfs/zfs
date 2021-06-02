include $(top_srcdir)/config/Substfiles.am
include $(top_srcdir)/config/Shellcheck.am

bashcompletiondir = $(sysconfdir)/bash_completion.d

noinst_DATA = zfs

EXTRA_DIST += $(noinst_DATA)
SUBSTFILES += $(noinst_DATA)

SHELLCHECKSCRIPTS = $(noinst_DATA)
SHELLCHECK_SHELL = bash
SHELLCHECK_IGNORE = ,SC2207
