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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

include $(SRC)/Makefile.master

ROOTOPTPKG = $(ROOT)/opt/zfs-tests
TESTDIR = $(ROOTOPTPKG)/tests/functional/reservation

PROGS = cleanup \
	reservation_001_pos \
	reservation_002_pos \
	reservation_003_pos \
	reservation_004_pos \
	reservation_005_pos \
	reservation_006_pos \
	reservation_007_pos \
	reservation_008_pos \
	reservation_009_pos \
	reservation_010_pos \
	reservation_011_pos \
	reservation_012_pos \
	reservation_013_pos \
	reservation_014_pos \
	reservation_015_pos \
	reservation_016_pos \
	reservation_017_pos \
	reservation_018_pos \
	setup

FILES = reservation.cfg \
	reservation.shlib

CMDS = $(PROGS:%=$(TESTDIR)/%) $(FILES:%=$(TESTDIR)/%)
$(CMDS) := FILEMODE = 0555

all lint clean clobber:

install: $(CMDS)

$(CMDS): $(TESTDIR)

$(TESTDIR):
	$(INS.dir)

$(TESTDIR)/%: %.sh
	$(INS.rename)

$(TESTDIR)/%: %.ksh
	$(INS.rename)

$(TESTDIR)/%: %
	$(INS.file)
