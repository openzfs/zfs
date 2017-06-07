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
TESTDIR = $(ROOTOPTPKG)/tests/functional/acl/trivial

PROGS = cleanup \
	setup \
	zfs_acl_aclmode_restricted_001_pos \
	zfs_acl_chmod_001_pos \
	zfs_acl_compress_001_pos \
	zfs_acl_cp_001_pos \
	zfs_acl_cp_002_neg \
	zfs_acl_cp_003_neg \
	zfs_acl_find_001_pos \
	zfs_acl_find_002_neg \
	zfs_acl_ls_001_pos \
	zfs_acl_ls_002_neg \
	zfs_acl_mv_001_pos \
	zfs_acl_pack_001_pos \
	zfs_acl_pax_001_pos \
	zfs_acl_pax_002_pos \
	zfs_acl_pax_003_pos \
	zfs_acl_pax_004_pos \
	zfs_acl_pax_005_pos \
	zfs_acl_pax_006_pos \
	zfs_acl_tar_001_pos \
	zfs_acl_tar_002_neg

CMDS = $(PROGS:%=$(TESTDIR)/%)
$(CMDS) := FILEMODE = 0555

all lint clean clobber:

install: $(CMDS)

$(CMDS): $(TESTDIR)

$(TESTDIR):
	$(INS.dir)

$(TESTDIR)/%: %.ksh
	$(INS.rename)
