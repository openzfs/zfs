include $(top_srcdir)/config/Shellcheck.am
include $(top_srcdir)/config/Rules.am
include $(top_srcdir)/config/CppCheck.am

ACLOCAL_AMFLAGS = -I config

SUBDIRS = include
if BUILD_LINUX
SUBDIRS += rpm
endif

EXTRA_DIST =
CPPCHECKDIRS += cmd etc/systemd/system-generators
if CONFIG_USER
SUBDIRS += scripts . man tests cmd etc contrib
include $(srcdir)/%D%/lib/Makefile.am
if BUILD_LINUX
SUBDIRS += udev
endif
endif
CPPCHECKDIRS += module
if CONFIG_KERNEL
SUBDIRS += module

extradir = $(prefix)/src/zfs-$(VERSION)
extra_HEADERS = zfs.release.in zfs_config.h.in
endif

EXTRA_DIST += autogen.sh copy-builtin
EXTRA_DIST += config/config.awk config/rpm.am config/deb.am config/tgz.am
EXTRA_DIST += AUTHORS CODE_OF_CONDUCT.md COPYRIGHT LICENSE META NEWS NOTICE
EXTRA_DIST += README.md RELEASES.md
EXTRA_DIST += module/lua/README.zfs module/os/linux/spl/README.md

# Include all the extra licensing information for modules
EXTRA_DIST += module/icp/algs/skein/THIRDPARTYLICENSE
EXTRA_DIST += module/icp/algs/skein/THIRDPARTYLICENSE.descrip
EXTRA_DIST += module/icp/asm-x86_64/aes/THIRDPARTYLICENSE.gladman
EXTRA_DIST += module/icp/asm-x86_64/aes/THIRDPARTYLICENSE.gladman.descrip
EXTRA_DIST += module/icp/asm-x86_64/aes/THIRDPARTYLICENSE.openssl
EXTRA_DIST += module/icp/asm-x86_64/aes/THIRDPARTYLICENSE.openssl.descrip
EXTRA_DIST += module/icp/asm-x86_64/modes/THIRDPARTYLICENSE.cryptogams
EXTRA_DIST += module/icp/asm-x86_64/modes/THIRDPARTYLICENSE.cryptogams.descrip
EXTRA_DIST += module/icp/asm-x86_64/modes/THIRDPARTYLICENSE.openssl
EXTRA_DIST += module/icp/asm-x86_64/modes/THIRDPARTYLICENSE.openssl.descrip
EXTRA_DIST += module/os/linux/spl/THIRDPARTYLICENSE.gplv2
EXTRA_DIST += module/os/linux/spl/THIRDPARTYLICENSE.gplv2.descrip
EXTRA_DIST += module/zfs/THIRDPARTYLICENSE.cityhash
EXTRA_DIST += module/zfs/THIRDPARTYLICENSE.cityhash.descrip

@CODE_COVERAGE_RULES@

GITREV = include/zfs_gitrev.h

PHONY += gitrev
gitrev:
	$(AM_V_GEN)$(top_srcdir)/scripts/make_gitrev.sh $(GITREV)

all: gitrev

# Double-colon rules are allowed; there are multiple independent definitions.
maintainer-clean-local::
	-$(RM) $(GITREV)

distclean-local::
	-$(RM) -R autom4te*.cache build
	-find . \( -name SCCS -o -name BitKeeper -o -name .svn -o -name CVS \
		-o -name .pc -o -name .hg -o -name .git \) -prune -o \
		\( -name '*.orig' -o -name '*.rej' -o -name '*~' \
		-o -name '*.bak' -o -name '#*#' -o -name '.*.orig' \
		-o -name '.*.rej' -o -size 0 -o -name '*%' -o -name '.*.cmd' \
		-o -name 'core' -o -name 'Makefile' -o -name 'Module.symvers' \
		-o -name '*.order' -o -name '*.markers' -o -name '*.gcda' \
		-o -name '*.gcno' \) \
		-type f -delete

all-local:
	-SCRIPT_COMMON=$(top_builddir)/scripts/common.sh $(top_srcdir)/scripts/zfs-tests.sh -c

dist-hook:
	$(AM_V_GEN)$(top_srcdir)/scripts/make_gitrev.sh -D $(distdir) $(GITREV)
	$(SED) ${ac_inplace} -e 's/Release:[[:print:]]*/Release:      $(RELEASE)/' \
		$(distdir)/META

PHONY += codecheck
codecheck: cstyle shellcheck checkbashisms flake8 mancheck testscheck vcscheck zstdcheck

PHONY += checkstyle
checkstyle: codecheck commitcheck

PHONY += commitcheck
commitcheck:
	@if git rev-parse --git-dir > /dev/null 2>&1; then \
		${top_srcdir}/scripts/commitcheck.sh; \
	fi

if HAVE_PARALLEL
cstyle_line = -print0 | parallel -X0 ${top_srcdir}/scripts/cstyle.pl -cpP {}
else
cstyle_line = -exec ${top_srcdir}/scripts/cstyle.pl -cpP {} +
endif
PHONY += cstyle
cstyle:
	@find ${top_srcdir} -name build -prune \
		-o -type f -name '*.[hc]' \
		! -name 'zfs_config.*' ! -name '*.mod.c' \
		! -name 'opt_global.h' ! -name '*_if*.h' \
		! -name 'zstd_compat_wrapper.h' \
		! -path './module/zstd/lib/*' \
		! -path './include/sys/lua/*' \
		! -path './module/lua/l*.[ch]' \
		! -path './module/zfs/lz4.c' \
		$(cstyle_line)

filter_executable = -exec test -x '{}' \; -print

SHELLCHECKDIRS = cmd contrib etc scripts tests
SHELLCHECKSCRIPTS = autogen.sh

PHONY += mancheck
mancheck:
	${top_srcdir}/scripts/mancheck.sh ${top_srcdir}/man ${top_srcdir}/tests/test-runner/man

PHONY += testscheck
testscheck:
	@[ $$(find ${top_srcdir}/tests/zfs-tests -type f \
		\( -name '*.ksh' -not ${filter_executable} \) -o \
		\( -name '*.kshlib' ${filter_executable} \) -o \
		\( -name '*.shlib' ${filter_executable} \) -o \
		\( -name '*.cfg' ${filter_executable} \) | \
		tee /dev/stderr | wc -l) -eq 0 ]

PHONY += vcscheck
vcscheck:
	@if git rev-parse --git-dir > /dev/null 2>&1; then \
		git ls-files . --exclude-standard --others | \
		awk '{c++; print} END {if(c>0) exit 1}' ; \
	fi

PHONY += zstdcheck
zstdcheck:
	@$(MAKE) -C module check-zstd-symbols

PHONY += lint
lint: cppcheck paxcheck

PHONY += paxcheck
paxcheck:
	@if type scanelf > /dev/null 2>&1; then \
		${top_srcdir}/scripts/paxcheck.sh ${top_builddir}; \
	else \
		echo "skipping paxcheck because scanelf is not installed"; \
	fi

PHONY += flake8
flake8:
	@if type flake8 > /dev/null 2>&1; then \
		flake8 ${top_srcdir}; \
	else \
		echo "skipping flake8 because flake8 is not installed"; \
	fi

PHONY += ctags
ctags:
	$(RM) tags
	find $(top_srcdir) -name '.?*' -prune \
		-o -type f -name '*.[hcS]' -exec ctags -a {} +

PHONY += etags
etags:
	$(RM) TAGS
	find $(top_srcdir) -name '.?*' -prune \
		-o -type f -name '*.[hcS]' -exec etags -a {} +

PHONY += cscopelist
cscopelist:
	find $(top_srcdir) -name '.?*' -prune \
		-o -type f -name '*.[hc]' -print >cscope.files

PHONY += tags
tags: ctags etags

PHONY += pkg pkg-dkms pkg-kmod pkg-utils
pkg: @DEFAULT_PACKAGE@
pkg-dkms: @DEFAULT_PACKAGE@-dkms
pkg-kmod: @DEFAULT_PACKAGE@-kmod
pkg-utils: @DEFAULT_PACKAGE@-utils

include config/rpm.am
include config/deb.am
include config/tgz.am

.PHONY: $(PHONY)
