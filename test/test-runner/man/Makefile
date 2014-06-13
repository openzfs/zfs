#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2012 by Delphix. All rights reserved.
#

include $(SRC)/Makefile.master

MANSRCS = run.1

ROOTOPTPKG = $(ROOT)/opt/test-runner
MANDIR = $(ROOTOPTPKG)/man/man1

FILES = $(MANSRCS:%=$(MANDIR)/%)
$(FILES) := FILEMODE = 0444

all: $(MANSRCS)

install: $(FILES)

clean lint clobber:

$(FILES): $(MANDIR) $(MANSRCS)

$(MANDIR):
	$(INS.dir)

$(MANDIR)/%: %
	$(INS.file)
