SUBDIRS = lib cmd module include scripts

AUTOMAKE_OPTIONS = foreign dist-zip
EXTRA_DIST = autogen.sh spl.spec.in META DISCLAIMER
noinst_HEADERS = spl_config.h

distclean-local::
	-$(RM) -R autom4te*.cache
	-find . \( -name SCCS -o -name BitKeeper -o -name .svn -o -name CVS \
		-o -name .pc -o -name .hg -o -name .git \) -prune -o \
		\( -name '*.orig' -o -name '*.rej' -o -name '*~' \
		-o -name '*.bak' -o -name '#*#' -o -name '.*.orig' \
		-o -name '.*.rej' -o -name 'aclocal.m4' -o -size 0 \
		-o -name '*%' -o -name '.*.cmd' -o -name 'core' \
		-o -name 'Makefile' -o -name 'Module.symvers' \) \
		-type f -print | xargs $(RM)

install-data-local:
	$(INSTALL) -D spl_config.h $(DESTDIR)/$(LINUX)/include/spl/spl_config.h

ctags:
	$(RM) $(top_srcdir)/tags
	find $(top_srcdir) -name .git -prune -o -name '*.[hc]' | xargs ctags

etags:
	$(RM) $(top_srcdir)/TAGS
	find $(top_srcdir) -name .pc -prune -o -name '*.[hc]' | xargs etags -a

tags: ctags etags

rpm-local:
	mkdir -p $(rpmbuild)/TMP && \
	mkdir -p $(rpmbuild)/BUILD && \
	mkdir -p $(rpmbuild)/RPMS && \
	mkdir -p $(rpmbuild)/SRPMS && \
	mkdir -p $(rpmbuild)/SPECS && cp $(PACKAGE).spec $(rpmbuild)/SPECS && \
	mkdir -p $(rpmbuild)/SOURCES && cp $(distdir).tar.gz $(rpmbuild)/SOURCES

srpm: dist
	rpmbuild=`mktemp -t -d $(PACKAGE)-build-$$USER-XXXXXXXX`; \
	$(MAKE) $(AM_MAKEFLAGS) rpmbuild="$$rpmbuild" rpm-local || exit 1; \
	/usr/bin/rpmbuild --define "_tmppath $$rpmbuild/TMP" --define "_topdir $$rpmbuild" --define "build_src_rpm 1" --define "dist %{nil}" --nodeps -bs $$rpmbuild/SPECS/$(PACKAGE).spec || exit 1; \
	cp $$rpmbuild/SRPMS/$(distdir)-$(SPL_META_RELEASE).src.rpm . || exit 1;\
	$(RM) -R $$rpmbuild

# Use 'make rpm LINUX_VERSION=2.x.y-z' to rebuild the source RPM
# against any installed kernel-devel-2.x.y-z package.  This will
# override the LINUX_VERSION detected at configure time.
rpm: srpm
	rpmbuild=`mktemp -t -d $(PACKAGE)-build-$$USER-XXXXXXXX`; \
	$(MAKE) $(AM_MAKEFLAGS) rpmbuild="$$rpmbuild" rpm-local || exit 1; \
	/usr/bin/rpmbuild --define "_tmppath $$rpmbuild/TMP" --define "_topdir $$rpmbuild" --define "dist %{nil}" --define "require_kver $(LINUX_VERSION)" --nodeps --rebuild $(distdir)-$(SPL_META_RELEASE).src.rpm || exit 1; \
	cp $$rpmbuild/RPMS/*/* . || exit 1; \
	$(RM) -R $$rpmbuild
