#! /bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
