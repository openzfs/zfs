AUTOMAKE_OPTIONS = foreign dist-zip

SUBDIRS = lib cmd module include scripts
CONFIG_CLEAN_FILES =  aclocal.m4 config.guess config.sub
CONFIG_CLEAN_FILES += depcomp install-sh missing mkinstalldirs
EXTRA_DIST = autogen.sh

rpms: dist Makefile
	rpmbuild -ta $(distdir).tar.gz

