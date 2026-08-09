#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright 2024, iXsystems Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib

#
# DESCRIPTION:
# Verify zfs mount -R <filesystems/s> functionality.
#
# STRATEGY:
# 1. Create nested datasets
# 2. Unmount all datasets
# 3. Recusrively mount root datasets, this should mount all datasets
#    present in a pool
# 4. Unmount all datasets
# 5. Recusrsively mount child datasets with children. This should mount
#    child datasets, but not the root dataset or parent datasets
# 6. Unmount all datasets
# 7. Mount root dataset recursively again and confirm all child
#    datasets are mounted.
#

verify_runnable "both"

function cleanup
{
	log_must datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -R
	log_must datasetexists $TESTPOOL/$TESTFS2 && \
		destroy_dataset $TESTPOOL/$TESTFS2 -R
	log_must datasetexists $TESTPOOL/$TESTFS3 && \
		destroy_dataset $TESTPOOL/$TESTFS3 -R
}

function setup_all
{
	log_must datasetexists $TESTPOOL/$TESTFS || zfs create $TESTPOOL/$TESTFS
	log_must zfs create $TESTPOOL/$TESTFS1
	log_must zfs create $TESTPOOL/$TESTFS2
	log_must zfs create $TESTPOOL/$TESTFS3
	log_must zfs create $TESTPOOL/$TESTFS2/child1
	log_must zfs create $TESTPOOL/$TESTFS2/child2
	log_must zfs create $TESTPOOL/$TESTFS2/child3
	log_must zfs create $TESTPOOL/$TESTFS2/child2/subchild
	log_must zfs create $TESTPOOL/$TESTFS3/child
}

log_assert "Verify that 'zfs $mountrecursive' successfully, " \
	"mounts the dataset along with all its children."

log_onexit cleanup

log_must setup_all

log_must zfs $unmountall

log_must zfs $mountrecursive $TESTPOOL

log_must mounted $TESTPOOL
log_must mounted $TESTPOOL/$TESTFS
log_must mounted $TESTPOOL/$TESTFS1
log_must mounted $TESTPOOL/$TESTFS2
log_must mounted $TESTPOOL/$TESTFS3
log_must mounted $TESTPOOL/$TESTFS2/child1
log_must mounted $TESTPOOL/$TESTFS2/child2
log_must mounted $TESTPOOL/$TESTFS2/child3
log_must mounted $TESTPOOL/$TESTFS2/child2/subchild
log_must mounted $TESTPOOL/$TESTFS3/child

log_must zfs $unmountall

log_mustnot mounted $TESTPOOL
log_mustnot mounted $TESTPOOL/$TESTFS
log_mustnot mounted $TESTPOOL/$TESTFS1
log_mustnot mounted $TESTPOOL/$TESTFS2
log_mustnot mounted $TESTPOOL/$TESTFS3
log_mustnot mounted $TESTPOOL/$TESTFS2/child1
log_mustnot mounted $TESTPOOL/$TESTFS2/child2
log_mustnot mounted $TESTPOOL/$TESTFS2/child3
log_mustnot mounted $TESTPOOL/$TESTFS2/child2/subchild
log_mustnot mounted $TESTPOOL/$TESTFS3/child

log_must zfs $mountrecursive $TESTPOOL/$TESTFS2 $TESTPOOL/$TESTFS3

log_mustnot mounted $TESTPOOL
log_mustnot mounted $TESTPOOL/$TESTFS
log_mustnot mounted $TESTPOOL/$TESTFS1
log_must mounted $TESTPOOL/$TESTFS2
log_must mounted $TESTPOOL/$TESTFS3
log_must mounted $TESTPOOL/$TESTFS2/child1
log_must mounted $TESTPOOL/$TESTFS2/child2
log_must mounted $TESTPOOL/$TESTFS2/child3
log_must mounted $TESTPOOL/$TESTFS2/child2/subchild
log_must mounted $TESTPOOL/$TESTFS3/child

log_must zfs $unmountall

log_mustnot mounted $TESTPOOL
log_mustnot mounted $TESTPOOL/$TESTFS
log_mustnot mounted $TESTPOOL/$TESTFS1
log_mustnot mounted $TESTPOOL/$TESTFS2
log_mustnot mounted $TESTPOOL/$TESTFS3
log_mustnot mounted $TESTPOOL/$TESTFS2/child1
log_mustnot mounted $TESTPOOL/$TESTFS2/child2
log_mustnot mounted $TESTPOOL/$TESTFS2/child3
log_mustnot mounted $TESTPOOL/$TESTFS2/child2/subchild
log_mustnot mounted $TESTPOOL/$TESTFS3/child

log_must zfs $mountrecursive $TESTPOOL/$TESTFS2/child2

log_must mounted $TESTPOOL/$TESTFS2/child2
log_must mounted $TESTPOOL/$TESTFS2/child2/subchild
log_mustnot mounted $TESTPOOL
log_mustnot mounted $TESTPOOL/$TESTFS
log_mustnot mounted $TESTPOOL/$TESTFS1
log_mustnot mounted $TESTPOOL/$TESTFS2
log_mustnot mounted $TESTPOOL/$TESTFS3
log_mustnot mounted $TESTPOOL/$TESTFS2/child1
log_mustnot mounted $TESTPOOL/$TESTFS2/child3
log_mustnot mounted $TESTPOOL/$TESTFS3/child

log_pass "'zfs $mountrecursive' behaves as expected."
