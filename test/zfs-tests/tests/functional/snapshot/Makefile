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
TESTDIR = $(ROOTOPTPKG)/tests/functional/snapshot

PROGS = cleanup \
	clone_001_pos \
	rollback_001_pos \
	rollback_002_pos \
	rollback_003_pos \
	setup \
	snapshot_001_pos \
	snapshot_002_pos \
	snapshot_003_pos \
	snapshot_004_pos \
	snapshot_005_pos \
	snapshot_006_pos \
	snapshot_007_pos \
	snapshot_008_pos \
	snapshot_009_pos \
	snapshot_010_pos \
	snapshot_011_pos \
	snapshot_012_pos \
	snapshot_013_pos \
	snapshot_014_pos \
	snapshot_015_pos \
	snapshot_016_pos \
	snapshot_017_pos

FILES = snapshot.cfg

CMDS = $(PROGS:%=$(TESTDIR)/%) $(FILES:%=$(TESTDIR)/%)
$(CMDS) := FILEMODE = 0555

all lint clean clobber:

install: $(CMDS)

$(CMDS): $(TESTDIR)

$(TESTDIR):
	$(INS.dir)

$(TESTDIR)/%: %.ksh
	$(INS.rename)

$(TESTDIR)/%: %
	$(INS.file)
