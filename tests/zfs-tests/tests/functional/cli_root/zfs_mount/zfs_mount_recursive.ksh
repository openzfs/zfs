#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
