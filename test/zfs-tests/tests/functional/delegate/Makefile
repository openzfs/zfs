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
TESTDIR = $(ROOTOPTPKG)/tests/functional/delegate

PROGS = cleanup \
	setup \
	zfs_allow_001_pos \
	zfs_allow_002_pos \
	zfs_allow_003_pos \
	zfs_allow_004_pos \
	zfs_allow_005_pos \
	zfs_allow_006_pos \
	zfs_allow_007_pos \
	zfs_allow_008_pos \
	zfs_allow_009_neg \
	zfs_allow_010_pos \
	zfs_allow_011_neg \
	zfs_allow_012_neg \
	zfs_unallow_001_pos \
	zfs_unallow_002_pos \
	zfs_unallow_003_pos \
	zfs_unallow_004_pos \
	zfs_unallow_005_pos \
	zfs_unallow_006_pos \
	zfs_unallow_007_neg \
	zfs_unallow_008_neg

FILES = delegate.cfg \
	delegate_common.kshlib

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
