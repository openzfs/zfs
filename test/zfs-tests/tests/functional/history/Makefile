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
TESTDIR = $(ROOTOPTPKG)/tests/functional/history

PROGS = cleanup \
	history_001_pos \
	history_002_pos \
	history_003_pos \
	history_004_pos \
	history_005_neg \
	history_006_neg \
	history_007_pos \
	history_008_pos \
	history_009_pos \
	history_010_pos \
	setup

FILES = history.cfg \
	history_common.kshlib

DATAFILES = i386.migratedpool.DAT.Z \
	i386.orig_history.txt \
	sparc.migratedpool.DAT.Z \
	sparc.orig_history.txt \
	zfs-pool-v4.dat.Z

CMDS = $(PROGS:%=$(TESTDIR)/%) $(FILES:%=$(TESTDIR)/%)
$(CMDS) := FILEMODE = 0555

DATA = $(DATAFILES:%=$(TESTDIR)/%)
$(DATA) := FILEMODE = 0444

all lint clean clobber:

install: $(CMDS) $(DATA)

$(CMDS): $(TESTDIR)

$(DATA): $(TESTDIR)

$(TESTDIR):
	$(INS.dir)

$(TESTDIR)/%: %.ksh
	$(INS.rename)

$(TESTDIR)/%: %
	$(INS.file)
