# All headers are referenced by this top level Makefile.am are
# noinst_HEADERS because they are not installed in the usual include
# location.  We do not want to be using $includedir for this.
# Installation is handled by the custom install-data-local rule.
noinst_HEADERS  = *.h
noinst_HEADERS += asm/*.h
noinst_HEADERS += fs/*.h
noinst_HEADERS += linux/*.h
noinst_HEADERS += rpc/*.h
noinst_HEADERS += sharefs/*.h
noinst_HEADERS += sys/fm/*.h
noinst_HEADERS += sys/fs/*.h
noinst_HEADERS += sys/sysevent/*.h
noinst_HEADERS += sys/*.h
noinst_HEADERS += util/*.h
noinst_HEADERS += vm/*.h

install-data-local:
	instdest=$(DESTDIR)/$(LINUX)/include/spl; \
	instfiles=`find . -name '*.h'`; \
        for instfile in $$instfiles; do \
		$(INSTALL) -D $$instfile $$instdest/$$instfile; \
        done
