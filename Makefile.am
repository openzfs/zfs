CLEANFILES =
dist_noinst_DATA =
INSTALL_DATA_HOOKS =
INSTALL_EXEC_HOOKS =
ALL_LOCAL =
CLEAN_LOCAL =
CHECKS = shellcheck checkbashisms

include $(top_srcdir)/config/Rules.am
include $(top_srcdir)/config/CppCheck.am
include $(top_srcdir)/config/Shellcheck.am
include $(top_srcdir)/config/Substfiles.am
include $(top_srcdir)/scripts/Makefile.am

ACLOCAL_AMFLAGS = -I config

SUBDIRS = include
if BUILD_LINUX
include $(srcdir)/%D%/rpm/Makefile.am
endif

if CONFIG_USER
include $(srcdir)/%D%/cmd/Makefile.am
include $(srcdir)/%D%/contrib/Makefile.am
include $(srcdir)/%D%/etc/Makefile.am
include $(srcdir)/%D%/lib/Makefile.am
include $(srcdir)/%D%/man/Makefile.am
include $(srcdir)/%D%/tests/Makefile.am
if BUILD_LINUX
include $(srcdir)/%D%/udev/Makefile.am
endif
endif
CPPCHECKDIRS += module
if CONFIG_KERNEL
SUBDIRS += module

extradir = $(prefix)/src/zfs-$(VERSION)
extra_HEADERS = zfs.release.in zfs_config.h.in
endif

dist_noinst_DATA += autogen.sh copy-builtin
dist_noinst_DATA += AUTHORS CODE_OF_CONDUCT.md COPYRIGHT LICENSE META NEWS NOTICE
dist_noinst_DATA += README.md RELEASES.md
dist_noinst_DATA += module/lua/README.zfs module/os/linux/spl/README.md

# Include all the extra licensing information for modules
dist_noinst_DATA += module/icp/algs/skein/THIRDPARTYLICENSE
dist_noinst_DATA += module/icp/algs/skein/THIRDPARTYLICENSE.descrip
dist_noinst_DATA += module/icp/asm-x86_64/aes/THIRDPARTYLICENSE.gladman
dist_noinst_DATA += module/icp/asm-x86_64/aes/THIRDPARTYLICENSE.gladman.descrip
dist_noinst_DATA += module/icp/asm-x86_64/aes/THIRDPARTYLICENSE.openssl
dist_noinst_DATA += module/icp/asm-x86_64/aes/THIRDPARTYLICENSE.openssl.descrip
dist_noinst_DATA += module/icp/asm-x86_64/modes/THIRDPARTYLICENSE.cryptogams
dist_noinst_DATA += module/icp/asm-x86_64/modes/THIRDPARTYLICENSE.cryptogams.descrip
dist_noinst_DATA += module/icp/asm-x86_64/modes/THIRDPARTYLICENSE.openssl
dist_noinst_DATA += module/icp/asm-x86_64/modes/THIRDPARTYLICENSE.openssl.descrip
dist_noinst_DATA += module/os/linux/spl/THIRDPARTYLICENSE.gplv2
dist_noinst_DATA += module/os/linux/spl/THIRDPARTYLICENSE.gplv2.descrip
dist_noinst_DATA += module/zfs/THIRDPARTYLICENSE.cityhash
dist_noinst_DATA += module/zfs/THIRDPARTYLICENSE.cityhash.descrip

@CODE_COVERAGE_RULES@

GITREV = include/zfs_gitrev.h
CLEANFILES += $(GITREV)
PHONY += gitrev
gitrev:
	$(AM_V_GEN)$(top_srcdir)/scripts/make_gitrev.sh $(GITREV)

all: gitrev

PHONY += install-data-hook $(INSTALL_DATA_HOOKS)
install-data-hook: $(INSTALL_DATA_HOOKS)

PHONY += install-exec-hook $(INSTALL_EXEC_HOOKS)
install-exec-hook: $(INSTALL_EXEC_HOOKS)

PHONY += maintainer-clean-local
maintainer-clean-local:
	-$(RM) $(GITREV)

PHONY += distclean-local
distclean-local:
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

PHONY += $(CLEAN_LOCAL)
clean-local: $(CLEAN_LOCAL)

PHONY += $(ALL_LOCAL)
all-local: $(ALL_LOCAL)

dist-hook:
	$(top_srcdir)/scripts/make_gitrev.sh -D $(distdir) $(GITREV)
	$(SED) $(ac_inplace) 's/\(Release:[[:space:]]*\).*/\1$(RELEASE)/' $(distdir)/META

PHONY += codecheck $(CHECKS)
codecheck: $(CHECKS)

SHELLCHECKSCRIPTS += autogen.sh

PHONY += checkstyle
checkstyle: codecheck commitcheck

PHONY += commitcheck
commitcheck:
	$(AM_V_at)if git rev-parse --git-dir > /dev/null 2>&1; then \
		${top_srcdir}/scripts/commitcheck.sh; \
	fi

CHECKS += spdxcheck
spdxcheck:
	$(AM_V_at)$(top_srcdir)/scripts/spdxcheck.pl

if HAVE_PARALLEL
cstyle_line = -print0 | parallel -X0 ${top_srcdir}/scripts/cstyle.pl -cpP {}
else
cstyle_line = -exec ${top_srcdir}/scripts/cstyle.pl -cpP {} +
endif
CHECKS += cstyle
cstyle:
	$(AM_V_at)find $(top_srcdir) -name build -prune \
		-o -type f -name '*.[hc]' \
		! -name 'zfs_config.*' ! -name '*.mod.c' \
		! -name 'opt_global.h' ! -name '*_if*.h' \
		! -name 'zstd_compat_wrapper.h' \
		! -name 'monocypher.[ch]' \
		! -path './module/zstd/lib/*' \
		! -path './include/sys/lua/*' \
		! -path './module/lua/l*.[ch]' \
		! -path './module/zfs/lz4.c' \
		$(cstyle_line)

filter_executable = -exec test -x '{}' \; -print
CHECKS += testscheck
testscheck:
	$(AM_V_at)[ $$(find $(top_srcdir)/tests/zfs-tests -type f \
		\( -name '*.ksh' -not $(filter_executable) \) -o \
		\( -name '*.kshlib' $(filter_executable) \) -o \
		\( -name '*.shlib' $(filter_executable) \) -o \
		\( -name '*.cfg' $(filter_executable) \) | \
		tee /dev/stderr | wc -l) -eq 0 ]

CHECKS += vcscheck
vcscheck:
	$(AM_V_at)if git rev-parse --git-dir > /dev/null 2>&1; then \
		git ls-files . --exclude-standard --others | \
		awk '{c++; print} END {if(c>0) exit 1}' ; \
	fi

CHECKS += zstdcheck
zstdcheck:
	@$(MAKE) -C module check-zstd-symbols

PHONY += lint
lint: cppcheck paxcheck

PHONY += paxcheck
paxcheck:
	$(AM_V_at)if type scanelf > /dev/null 2>&1; then \
		$(top_srcdir)/scripts/paxcheck.sh $(top_builddir); \
	else \
		echo "skipping paxcheck because scanelf is not installed"; \
	fi

CHECKS += flake8
flake8:
	$(AM_V_at)if type flake8 > /dev/null 2>&1; then \
		flake8 $(top_srcdir); \
	else \
		echo "skipping flake8 because flake8 is not installed"; \
	fi

PHONY += regen-tests
regen-tests:
	@$(MAKE) -C tests/zfs-tests/tests regen

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
