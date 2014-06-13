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
# Copyright 2014 Garrett D'Amore <garrett@damore.org>
# Copyright (c) 2012 by Delphix. All rights reserved.
#

include $(SRC)/Makefile.master

READMES = README

ROOTOPTPKG = $(ROOT)/opt/util-tests

FILES = $(READMES:%=$(ROOTOPTPKG)/%)
$(FILES) := FILEMODE = 0444

all: $(READMES)

install: $(ROOTOPTPKG) $(FILES)

clean lint clobber:

$(ROOTOPTPKG):
	$(INS.dir)

$(ROOTOPTPKG)/%: %
	$(INS.file)
