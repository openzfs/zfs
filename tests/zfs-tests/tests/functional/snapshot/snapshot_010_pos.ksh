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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/snapshot/snapshot.cfg

#
# DESCRIPTION:
#	Verify 'destroy -r' can correctly destroy a snapshot tree at any point.
#
# STRATEGY:
# 1. Use the snapshot -r to create snapshot for top level pool
# 2. Select a middle point of the snapshot tree, use destroy -r to destroy all
#	snapshots beneath the point.
# 3. Verify the destroy results.
#

verify_runnable "both"

function cleanup
{
	typeset snap

	destroy_dataset $ctrvol "-rf"

	for snap in $ctrfs@$TESTSNAP1 \
		$snappool $snapvol $snapctr $snapctrvol \
		$snapctrclone $snapctrfs
	do
		snapexists $snap && destroy_dataset $snap "-rf"
	done

}

log_assert "Verify 'destroy -r' can correctly destroy a snapshot subtree at any point."
log_onexit cleanup

ctr=$TESTPOOL/$TESTCTR
ctrfs=$ctr/$TESTFS1
ctrvol=$ctr/$TESTVOL1
snappool=$SNAPPOOL
snapfs=$SNAPFS
snapctr=$ctr@$TESTSNAP
snapvol=$SNAPFS1
snapctrvol=$ctr/$TESTVOL1@$TESTSNAP
snapctrclone=$ctr/$TESTCLONE@$TESTSNAP
snapctrfs=$SNAPCTR

#preparation for testing
log_must zfs snapshot $ctrfs@$TESTSNAP1
if is_global_zone; then
	log_must zfs create -V $VOLSIZE $ctrvol
else
	log_must zfs create $ctrvol
fi

log_must zfs snapshot -r $snappool
block_device_wait

#select the $TESTCTR as destroy point, $TESTCTR is a child of $TESTPOOL
log_must zfs destroy -r $snapctr
for snap in $snapctr $snapctrvol $snapctrclone $snapctrfs; do
	snapexists $snap && \
		log_fail "The snapshot $snap is not destroyed correctly."
done

for snap in $snappool $snapfs $snapvol $ctrfs@$TESTSNAP1; do
	! snapexists $snap && \
		log_fail "The snapshot $snap should be not destroyed."
done

log_pass  "'destroy -r' destroys snapshot subtree as expected."
