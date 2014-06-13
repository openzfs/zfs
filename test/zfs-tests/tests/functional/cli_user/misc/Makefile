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
TESTDIR = $(ROOTOPTPKG)/tests/functional/cli_user/misc

PROGS = cleanup \
	setup \
	zdb_001_neg \
	zfs_001_neg \
	zfs_allow_001_neg \
	zfs_clone_001_neg \
	zfs_create_001_neg \
	zfs_destroy_001_neg \
	zfs_get_001_neg \
	zfs_inherit_001_neg \
	zfs_mount_001_neg \
	zfs_promote_001_neg \
	zfs_receive_001_neg \
	zfs_rename_001_neg \
	zfs_rollback_001_neg \
	zfs_send_001_neg \
	zfs_set_001_neg \
	zfs_share_001_neg \
	zfs_snapshot_001_neg \
	zfs_unallow_001_neg \
	zfs_unmount_001_neg \
	zfs_unshare_001_neg \
	zfs_upgrade_001_neg \
	zpool_001_neg \
	zpool_add_001_neg \
	zpool_attach_001_neg \
	zpool_clear_001_neg \
	zpool_create_001_neg \
	zpool_destroy_001_neg \
	zpool_detach_001_neg \
	zpool_export_001_neg \
	zpool_get_001_neg \
	zpool_history_001_neg \
	zpool_import_001_neg \
	zpool_import_002_neg \
	zpool_offline_001_neg \
	zpool_online_001_neg \
	zpool_remove_001_neg \
	zpool_replace_001_neg \
	zpool_scrub_001_neg \
	zpool_set_001_neg \
	zpool_status_001_neg \
	zpool_upgrade_001_neg

FILES = misc.cfg

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
