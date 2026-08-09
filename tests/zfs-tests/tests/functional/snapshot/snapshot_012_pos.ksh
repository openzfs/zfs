#! /bin/ksh -p
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/snapshot/snapshot.cfg

#
# DESCRIPTION:
#	Verify 'snapshot -r' can create snapshot for promoted clone, and vice
#	versa, a clone filesystem from the snapshot created by 'snapshot -r'
#	can be correctly promoted.
#
# STRATEGY:
#	1. Create a dataset tree
#	2. snapshot a filesystem and clone the snapshot
#	3. promote the clone
#	4. snapshot -r the dataset tree
#	5. verify that the snapshot of cloned filesystem is created correctly
#	6. clone a snapshot from the snapshot tree
#	7. promote the clone
#	8. verify that the clone is promoted correctly.
#

verify_runnable "both"

function cleanup
{
	if datasetexists $clone1; then
		log_must zfs promote $ctrfs
		destroy_dataset $clone1
	fi

	snapexists $snapctr && destroy_dataset $snapctr -r

	if snapexists $clone@$TESTSNAP1; then
		log_must zfs promote $ctrfs
		destroy_dataset $ctrfs@$TESTSNAP1 -rR
	fi
}

log_assert "Verify that 'snapshot -r' can work with 'zfs promote'."
log_onexit cleanup

ctr=$TESTPOOL/$TESTCTR
ctrfs=$ctr/$TESTFS1
clone=$ctr/$TESTCLONE
clone1=$ctr/$TESTCLONE1
snappool=$SNAPPOOL
snapfs=$SNAPFS
snapctr=$ctr@$TESTSNAP
snapctrclone=$clone@$TESTSNAP
snapctrclone1=$clone1@$TESTSNAP
snapctrfs=$SNAPCTR

#preparation for testing
log_must zfs snapshot $ctrfs@$TESTSNAP1
log_must zfs clone $ctrfs@$TESTSNAP1 $clone
log_must zfs promote $clone

log_must zfs snapshot -r $snapctr

! snapexists $snapctrclone && \
	log_fail "'snapshot -r' fails to create $snapctrclone for $ctr/$TESTCLONE."

log_must zfs clone $snapctrfs $clone1
log_must zfs promote $clone1

#verify the origin value is correct.
orig_value=$(get_prop origin $ctrfs)
if ! snapexists $snapctrclone1 || [[ "$orig_value" != "$snapctrclone1" ]]; then
	log_fail "'zfs promote' fails to promote $clone which is cloned from \
		$snapctrfs."
fi

log_pass "'snapshot -r' can work with 'zfs promote' as expected."
