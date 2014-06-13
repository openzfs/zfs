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
TESTDIR = $(ROOTOPTPKG)/tests/functional/cli_root/zfs_unmount

PROGS = cleanup \
	setup \
	zfs_unmount_001_pos \
	zfs_unmount_002_pos \
	zfs_unmount_003_pos \
	zfs_unmount_004_pos \
	zfs_unmount_005_pos \
	zfs_unmount_006_pos \
	zfs_unmount_007_neg \
	zfs_unmount_008_neg \
	zfs_unmount_009_pos \
	zfs_unmount_all_001_pos

FILES = zfs_unmount.cfg \
	zfs_unmount.kshlib

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
