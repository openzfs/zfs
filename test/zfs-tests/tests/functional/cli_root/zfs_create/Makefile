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
TESTDIR = $(ROOTOPTPKG)/tests/functional/cli_root/zfs_create

PROGS = cleanup \
	setup \
	zfs_create_001_pos \
	zfs_create_002_pos \
	zfs_create_003_pos \
	zfs_create_004_pos \
	zfs_create_005_pos \
	zfs_create_006_pos \
	zfs_create_007_pos \
	zfs_create_008_neg \
	zfs_create_009_neg \
	zfs_create_010_neg \
	zfs_create_011_pos \
	zfs_create_012_pos \
	zfs_create_013_pos

FILES = properties.kshlib \
	zfs_create_common.kshlib \
	zfs_create.cfg

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
