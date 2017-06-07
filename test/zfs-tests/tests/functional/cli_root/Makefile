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

.PARALLEL: $(SUBDIRS)

include $(SRC)/Makefile.master

ROOTOPTPKG = $(ROOT)/opt/zfs-tests
TESTDIR = $(ROOTOPTPKG)/tests/functional/cli_root

PROGS = cli_common.kshlib

CMDS = $(PROGS:%=$(TESTDIR)/%)
$(CMDS) := FILEMODE = 0555

all lint clean clobber:

install: $(CMDS)

$(CMDS): $(TESTDIR)

$(TESTDIR):
	$(INS.dir)

$(TESTDIR)/%: %
	$(INS.file)

SUBDIRS = zdb \
	zfs \
	zfs_clone \
	zfs_copies \
	zfs_create \
	zfs_destroy \
	zfs_get \
	zfs_inherit \
	zfs_mount \
	zfs_promote \
	zfs_property \
	zfs_receive \
	zfs_rename \
	zfs_reservation \
	zfs_rollback \
	zfs_send \
	zfs_set \
	zfs_share \
	zfs_snapshot \
	zfs_unmount \
	zfs_unshare \
	zfs_upgrade \
	zpool \
	zpool_add \
	zpool_attach \
	zpool_clear \
	zpool_create \
	zpool_destroy \
	zpool_detach \
	zpool_expand \
	zpool_export \
	zpool_get \
	zpool_history \
	zpool_import \
	zpool_offline \
	zpool_online \
	zpool_remove \
	zpool_replace \
	zpool_scrub \
	zpool_set \
	zpool_status \
	zpool_upgrade

include $(SRC)/test/Makefile.com
