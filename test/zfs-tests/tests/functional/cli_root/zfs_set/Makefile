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
TESTDIR = $(ROOTOPTPKG)/tests/functional/cli_root/zfs_set

PROGS = cleanup \
	setup \
	cache_001_pos \
	cache_002_neg \
	canmount_001_pos \
	canmount_002_pos \
	canmount_003_pos \
	canmount_004_pos \
	checksum_001_pos \
	compression_001_pos \
	mountpoint_001_pos \
	mountpoint_002_pos \
	mountpoint_003_pos \
	onoffs_001_pos \
	property_alias_001_pos \
	readonly_001_pos \
	reservation_001_neg \
	ro_props_001_pos \
	share_mount_001_neg \
	snapdir_001_pos \
	user_property_001_pos \
	user_property_002_pos \
	user_property_003_neg \
	user_property_004_pos \
	version_001_neg \
	zfs_set_001_neg \
	zfs_set_002_neg \
	zfs_set_003_neg

FILES = zfs_set_common.kshlib

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
