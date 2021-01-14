ACLOCAL_AMFLAGS = -I config

SUBDIRS = include
if BUILD_LINUX
SUBDIRS += rpm
endif

if CONFIG_USER
SUBDIRS += etc man scripts lib tests cmd contrib
if BUILD_LINUX
SUBDIRS += udev
endif
endif
if CONFIG_KERNEL
SUBDIRS += module

extradir = $(prefix)/src/zfs-$(VERSION)
extra_HEADERS = zfs.release.in zfs_config.h.in

if BUILD_LINUX
kerneldir = $(prefix)/src/zfs-$(VERSION)/$(LINUX_VERSION)
nodist_kernel_HEADERS = zfs.release zfs_config.h module/$(LINUX_SYMBOLS)
endif
endif

AUTOMAKE_OPTIONS = foreign
EXTRA_DIST  = autogen.sh copy-builtin
EXTRA_DIST += cppcheck-suppressions.txt
EXTRA_DIST += config/config.awk config/rpm.am config/deb.am config/tgz.am
EXTRA_DIST += META AUTHORS COPYRIGHT LICENSE NEWS NOTICE README.md
EXTRA_DIST += CODE_OF_CONDUCT.md
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

PHONY = gitrev
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
		-type f -print | xargs $(RM)

all-local:
	-[ -x ${top_builddir}/scripts/zfs-tests.sh ] && \
	    ${top_builddir}/scripts/zfs-tests.sh -c

dist-hook:
	$(AM_V_GEN)$(top_srcdir)/scripts/make_gitrev.sh -D $(distdir) $(GITREV)
	$(SED) ${ac_inplace} -e 's/Release:[[:print:]]*/Release:      $(RELEASE)/' \
		$(distdir)/META

if BUILD_LINUX
# For compatibility, create a matching spl-x.y.z directly which contains
# symlinks to the updated header and object file locations.  These
# compatibility links will be removed in the next major release.
if CONFIG_KERNEL
install-data-hook:
	rm -rf $(DESTDIR)$(prefix)/src/spl-$(VERSION) && \
	mkdir $(DESTDIR)$(prefix)/src/spl-$(VERSION) && \
	cd $(DESTDIR)$(prefix)/src/spl-$(VERSION) && \
	ln -s ../zfs-$(VERSION)/include/spl include && \
	ln -s ../zfs-$(VERSION)/$(LINUX_VERSION) $(LINUX_VERSION) && \
	ln -s ../zfs-$(VERSION)/zfs_config.h.in spl_config.h.in && \
	ln -s ../zfs-$(VERSION)/zfs.release.in spl.release.in && \
	cd $(DESTDIR)$(prefix)/src/zfs-$(VERSION)/$(LINUX_VERSION) && \
	ln -fs zfs_config.h spl_config.h && \
	ln -fs zfs.release spl.release
endif
endif

PHONY += codecheck
codecheck: cstyle shellcheck checkbashisms flake8 mancheck testscheck vcscheck

PHONY += checkstyle
checkstyle: codecheck commitcheck

PHONY += commitcheck
commitcheck:
	@if git rev-parse --git-dir > /dev/null 2>&1; then \
		${top_srcdir}/scripts/commitcheck.sh; \
	fi

PHONY += cstyle
cstyle:
	@find ${top_srcdir} -name build -prune \
		-o -type f -name '*.[hc]' \
		! -name 'zfs_config.*' ! -name '*.mod.c' \
		! -name 'opt_global.h' ! -name '*_if*.h' \
		! -path './module/zstd/lib/*' \
		-exec ${top_srcdir}/scripts/cstyle.pl -cpP {} \+

filter_executable = -exec test -x '{}' \; -print

PHONY += shellcheck
shellcheck:
	@if type shellcheck > /dev/null 2>&1; then \
		shellcheck --exclude=SC1090 --exclude=SC1117 --format=gcc \
			$$(find ${top_srcdir}/scripts/*.sh -type f) \
			$$(find ${top_srcdir}/cmd/zed/zed.d/*.sh -type f) \
			$$(find ${top_srcdir}/cmd/zpool/zpool.d/* \
			-type f ${filter_executable}); \
	else \
		echo "skipping shellcheck because shellcheck is not installed"; \
	fi

PHONY += checkabi storeabi
checkabi: lib
	$(MAKE) -C lib checkabi

storeabi: lib
	$(MAKE) -C lib storeabi

PHONY += checkbashisms
checkbashisms:
	@if type checkbashisms > /dev/null 2>&1; then \
		checkbashisms -n -p -x \
			$$(find ${top_srcdir} \
				-name '.git' -prune \
				-o -name 'build' -prune \
				-o -name 'tests' -prune \
				-o -name 'config' -prune \
				-o -name 'zed-functions.sh*' -prune \
				-o -name 'zfs-import*' -prune \
				-o -name 'zfs-mount*' -prune \
				-o -name 'zfs-zed*' -prune \
				-o -name 'smart' -prune \
				-o -name 'paxcheck.sh' -prune \
				-o -name 'make_gitrev.sh' -prune \
				-o -name '90zfs' -prune \
				-o -type f ! -name 'config*' \
				! -name 'libtool' \
			-exec sh -c 'awk "NR==1 && /\#\!.*bin\/sh.*/ {print FILENAME;}" "{}"' \;); \
	else \
		echo "skipping checkbashisms because checkbashisms is not installed"; \
	fi

PHONY += mancheck
mancheck:
	@if type mandoc > /dev/null 2>&1; then \
		find ${top_srcdir}/man/man8 -type f -name 'zfs.8' \
			-o -name 'zpool.8' -o -name 'zdb.8' \
			-o -name 'zgenhostid.8' | \
			xargs mandoc -Tlint -Werror; \
	else \
		echo "skipping mancheck because mandoc is not installed"; \
	fi

if BUILD_LINUX
stat_fmt = -c '%A %n'
else
stat_fmt = -f '%Sp %N'
endif

PHONY += testscheck
testscheck:
	@find ${top_srcdir}/tests/zfs-tests -type f \
		\( -name '*.ksh' -not ${filter_executable} \) -o \
		\( -name '*.kshlib' ${filter_executable} \) -o \
		\( -name '*.shlib' ${filter_executable} \) -o \
		\( -name '*.cfg' ${filter_executable} \) | \
		xargs -r stat ${stat_fmt} | \
		awk '{c++; print} END {if(c>0) exit 1}'

PHONY += vcscheck
vcscheck:
	@if git rev-parse --git-dir > /dev/null 2>&1; then \
		git ls-files . --exclude-standard --others | \
		awk '{c++; print} END {if(c>0) exit 1}' ; \
	fi

PHONY += lint
lint: cppcheck paxcheck

PHONY += cppcheck
cppcheck:
	@if type cppcheck > /dev/null 2>&1; then \
		cppcheck --quiet --force --error-exitcode=2 --inline-suppr \
			--suppressions-list=${top_srcdir}/cppcheck-suppressions.txt \
			-UHAVE_SSE2 -UHAVE_AVX512F -UHAVE_UIO_ZEROCOPY \
			${top_srcdir}; \
	else \
		echo "skipping cppcheck because cppcheck is not installed"; \
	fi

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
		-o -type f -name '*.[hcS]' -print | xargs ctags -a

PHONY += etags
etags:
	$(RM) TAGS
	find $(top_srcdir) -name '.?*' -prune \
		-o -type f -name '*.[hcS]' -print | xargs etags -a

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
