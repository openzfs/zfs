#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.

# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg

#
# DESCRIPTION:
#	'zfs destroy -R <snapshot>' can destroy all the child
#	 snapshots and preserves all the nested datasets.
#
# STRATEGY:
#	1. Create nested datasets in the storage pool.
#	2. Create recursive snapshots for all the nested datasets.
#	3. Verify when snapshots are destroyed recursively, all
#          the nested datasets still exist.
#

verify_runnable "both"

log_assert "Verify 'zfs destroy -R <snapshot>' does not destroy" \
	"nested datasets."
log_onexit cleanup

datasets="$TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS1/$TESTFS2
    $TESTPOOL/$TESTFS1/$TESTFS2/$TESTFS3"

function cleanup
{
	for ds in $datasets; do
		datasetexists $ds && destroy_dataset $ds -rf
	done
}

# create nested datasets
log_must zfs create -p $TESTPOOL/$TESTFS1/$TESTFS2/$TESTFS3

# verify dataset creation
for ds in $datasets; do
	datasetexists $ds || log_fail "Create $ds dataset fail."
done

# create recursive nested snapshot
log_must zfs snapshot -r $TESTPOOL/$TESTFS1@snap
for ds in $datasets; do
	datasetexists $ds@snap || log_fail "Create $ds@snap snapshot fail."
done

# destroy nested snapshot recursively
log_must zfs destroy -R $TESTPOOL/$TESTFS1@snap

# verify snapshot destroy
for ds in $datasets; do
	datasetexists $ds@snap && log_fail "$ds@snap exists. Destroy failed!"
done

# verify nested datasets still exist
for ds in $datasets; do
	datasetexists $ds || log_fail "Recursive snapshot destroy deleted $ds"
done

log_pass "'zfs destroy -R <snapshot>' works as expected."
