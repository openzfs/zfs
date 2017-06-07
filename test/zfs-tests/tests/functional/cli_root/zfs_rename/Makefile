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

ROOTOPTPKG = $(ROOT)/opt/zfs-tests
TESTDIR = $(ROOTOPTPKG)/tests/functional/cli_root/zfs_rename

PROGS = cleanup \
	setup \
	zfs_rename_001_pos \
	zfs_rename_002_pos \
	zfs_rename_003_pos \
	zfs_rename_004_neg \
	zfs_rename_005_neg \
	zfs_rename_006_pos \
	zfs_rename_007_pos \
	zfs_rename_008_pos \
	zfs_rename_009_neg \
	zfs_rename_010_neg \
	zfs_rename_011_pos \
	zfs_rename_012_neg \
	zfs_rename_013_pos

FILES = zfs_rename.cfg \
	zfs_rename.kshlib

CMDS = $(PROGS:%=$(TESTDIR)/%) $(FILES:%=$(TESTDIR)/%)
$(CMDS) := FILEMODE = 0555

all lint clean clobber:

install: $(CMDS)

$(CMDS): $(TESTDIR)

$(TESTDIR):
	$(INS.dir)

$(TESTDIR)/%: %
	$(INS.file)

$(TESTDIR)/%: %.ksh
	$(INS.rename)
