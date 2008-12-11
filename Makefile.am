AUTOMAKE_OPTIONS = foreign dist-zip
EXTRA_DIST  = autogen.sh AUTHORS COPYING COPYRIGHT ChangeLog DISCLAIMER
EXTRA_DIST += GIT META OPENSOLARIS.LICENSE README ZFS.RELEASE

SUBDIRS = config doc scripts lib cmd module

.PHONY: rpm
rpms: dist-bzip2
	rpmbuild -ta $(distdir).tar.bz2
