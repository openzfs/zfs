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
TESTDIR = $(ROOTOPTPKG)/tests/functional/cli_root/zfs_destroy

PROGS = cleanup \
	setup \
	zfs_destroy_001_pos \
	zfs_destroy_002_pos \
	zfs_destroy_003_pos \
	zfs_destroy_004_pos \
	zfs_destroy_005_neg \
	zfs_destroy_006_neg \
	zfs_destroy_007_neg \
	zfs_destroy_008_pos \
	zfs_destroy_009_pos \
	zfs_destroy_010_pos \
	zfs_destroy_011_pos \
	zfs_destroy_012_pos \
	zfs_destroy_013_neg \
	zfs_destroy_014_pos \
	zfs_destroy_015_pos \
	zfs_destroy_016_pos

FILES = zfs_destroy_common.kshlib \
	zfs_destroy.cfg

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
