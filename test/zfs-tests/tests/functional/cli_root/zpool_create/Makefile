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
TESTDIR = $(ROOTOPTPKG)/tests/functional/cli_root/zpool_create

PROGS = cleanup \
	setup \
	zpool_create_001_pos \
	zpool_create_002_pos \
	zpool_create_003_pos \
	zpool_create_004_pos \
	zpool_create_005_pos \
	zpool_create_006_pos \
	zpool_create_007_neg \
	zpool_create_008_pos \
	zpool_create_009_neg \
	zpool_create_010_neg \
	zpool_create_011_neg \
	zpool_create_012_neg \
	zpool_create_013_neg \
	zpool_create_014_neg \
	zpool_create_015_neg \
	zpool_create_016_pos \
	zpool_create_017_neg \
	zpool_create_018_pos \
	zpool_create_019_pos \
	zpool_create_020_pos \
	zpool_create_021_pos \
	zpool_create_022_pos \
	zpool_create_023_neg \
	zpool_create_features_001_pos \
	zpool_create_features_002_pos \
	zpool_create_features_003_pos \
	zpool_create_features_004_neg

FILES = zpool_create.cfg \
	zpool_create.shlib

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
