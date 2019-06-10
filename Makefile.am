ACLOCAL_AMFLAGS = -I config

include config/rpm.am
include config/deb.am
include config/tgz.am

SUBDIRS = include rpm
if CONFIG_USER
SUBDIRS += udev etc man scripts lib tests cmd contrib
endif
if CONFIG_KERNEL
SUBDIRS += module

extradir = $(prefix)/src/zfs-$(VERSION)
extra_HEADERS = zfs.release.in zfs_config.h.in

kerneldir = $(prefix)/src/zfs-$(VERSION)/$(LINUX_VERSION)
nodist_kernel_HEADERS = zfs.release zfs_config.h module/$(LINUX_SYMBOLS)
endif

AUTOMAKE_OPTIONS = foreign
EXTRA_DIST  = autogen.sh copy-builtin
EXTRA_DIST += config/config.awk config/rpm.am config/deb.am config/tgz.am
EXTRA_DIST += META AUTHORS COPYRIGHT LICENSE NEWS NOTICE README.md
EXTRA_DIST += CODE_OF_CONDUCT.md

# Include all the extra licensing information for modules
EXTRA_DIST += module/icp/algs/skein/THIRDPARTYLICENSE
EXTRA_DIST += module/icp/algs/skein/THIRDPARTYLICENSE.descrip
EXTRA_DIST += module/icp/asm-x86_64/aes/THIRDPARTYLICENSE.gladman
EXTRA_DIST += module/icp/asm-x86_64/aes/THIRDPARTYLICENSE.gladman.descrip
EXTRA_DIST += module/icp/asm-x86_64/aes/THIRDPARTYLICENSE.openssl
EXTRA_DIST += module/icp/asm-x86_64/aes/THIRDPARTYLICENSE.openssl.descrip
EXTRA_DIST += module/spl/THIRDPARTYLICENSE.gplv2
EXTRA_DIST += module/spl/THIRDPARTYLICENSE.gplv2.descrip
EXTRA_DIST += module/zfs/THIRDPARTYLICENSE.cityhash
EXTRA_DIST += module/zfs/THIRDPARTYLICENSE.cityhash.descrip

@CODE_COVERAGE_RULES@

.PHONY: gitrev
gitrev:
	-${top_srcdir}/scripts/make_gitrev.sh

BUILT_SOURCES = gitrev

# Double-colon rules are allowed; there are multiple independent definitions.
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

dist-hook: gitrev
	cp ${top_srcdir}/include/zfs_gitrev.h $(distdir)/include; \
	sed -i 's/Release:[[:print:]]*/Release:      $(RELEASE)/' \
		$(distdir)/META

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

codecheck: cstyle shellcheck flake8 mancheck testscheck vcscheck

checkstyle: codecheck commitcheck

commitcheck:
	@if git rev-parse --git-dir > /dev/null 2>&1; then \
		${top_srcdir}/scripts/commitcheck.sh; \
	fi

cstyle:
	@find ${top_srcdir} -name build -prune -o -name '*.[hc]' \
		! -name 'zfs_config.*' ! -name '*.mod.c' -type f \
		-exec ${top_srcdir}/scripts/cstyle.pl -cpP {} \+

shellcheck:
	@if type shellcheck > /dev/null 2>&1; then \
		shellcheck --exclude=SC1090 --format=gcc \
			$$(find ${top_srcdir}/scripts/*.sh -type f) \
			$$(find ${top_srcdir}/cmd/zed/zed.d/*.sh -type f) \
			$$(find ${top_srcdir}/cmd/zpool/zpool.d/* -executable); \
	else \
		echo "skipping shellcheck because shellcheck is not installed"; \
	fi

mancheck:
	@if type mandoc > /dev/null 2>&1; then \
		find ${top_srcdir}/man/man8 -type f -name 'zfs.8' \
			-o -name 'zpool.8' -o -name 'zdb.8' \
			-o -name 'zgenhostid.8' | \
			xargs mandoc -Tlint -Werror; \
	else \
		echo "skipping mancheck because mandoc is not installed"; \
	fi

testscheck:
	@find ${top_srcdir}/tests/zfs-tests -type f \
		\( -name '*.ksh' -not -executable \) -o \
		\( -name '*.kshlib' -executable \) -o \
		\( -name '*.shlib' -executable \) -o \
		\( -name '*.cfg' -executable \) | \
		xargs -r stat -c '%A %n' | \
		awk '{c++; print} END {if(c>0) exit 1}'

vcscheck:
	@if git rev-parse --git-dir > /dev/null 2>&1; then \
		git ls-files . --exclude-standard --others | \
		awk '{c++; print} END {if(c>0) exit 1}' ; \
	fi

lint: cppcheck paxcheck

cppcheck:
	@if type cppcheck > /dev/null 2>&1; then \
		cppcheck --quiet --force --error-exitcode=2 --inline-suppr \
			--suppressions-list=.github/suppressions.txt \
			-UHAVE_SSE2 -UHAVE_AVX512F -UHAVE_UIO_ZEROCOPY \
			${top_srcdir}; \
	else \
		echo "skipping cppcheck because cppcheck is not installed"; \
	fi

paxcheck:
	@if type scanelf > /dev/null 2>&1; then \
		${top_srcdir}/scripts/paxcheck.sh ${top_srcdir}; \
	else \
		echo "skipping paxcheck because scanelf is not installed"; \
	fi

flake8:
	@if type flake8 > /dev/null 2>&1; then \
		flake8 ${top_srcdir}; \
	else \
		echo "skipping flake8 because flake8 is not installed"; \
	fi

ctags:
	$(RM) tags
	find $(top_srcdir) -name .git -prune -o -name '*.[hc]' | xargs ctags

etags:
	$(RM) TAGS
	find $(top_srcdir) -name .pc -prune -o -name '*.[hc]' | xargs etags -a

tags: ctags etags

pkg: @DEFAULT_PACKAGE@
pkg-dkms: @DEFAULT_PACKAGE@-dkms
pkg-kmod: @DEFAULT_PACKAGE@-kmod
pkg-utils: @DEFAULT_PACKAGE@-utils
