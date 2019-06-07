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
#	Verify 'snapshot -r' and 'destroy -r' can correctly create and destroy
#	snapshot tree respectively.
#
# STRATEGY:
# 1. Use the snapshot -r to create snapshot for top level pool
# 2. Verify the children snapshots are created correctly.
# 3. Use destroy -r to destroy the top level snapshot
# 4. Verify that all children snapshots are destroyed too.
#

verify_runnable "both"

function cleanup
{
	typeset ds
	typeset snap

	for ds in $ctr/$TESTVOL1 $ctr/$TESTCLONE; do
		destroy_dataset $ds "-rf"
	done

	for snap in $ctr/$TESTFS1@$TESTSNAP1 \
		$snappool $snapvol $snapctr $snapctrvol \
		$snapctrclone $snapctrfs
	do
		snapexists $snap && destroy_dataset $snap "-rf"
	done
}

log_assert "Verify snapshot -r can correctly create a snapshot tree."
log_onexit cleanup

ctr=$TESTPOOL/$TESTCTR
ctrfs=$ctr/$TESTFS1
ctrclone=$ctr/$TESTCLONE
ctrvol=$ctr/$TESTVOL1
snappool=$SNAPPOOL
snapfs=$SNAPFS
snapctr=$ctr@$TESTSNAP
snapvol=$SNAPFS1
snapctrvol=$ctrvol@$TESTSNAP
snapctrclone=$ctrclone@$TESTSNAP
snapctrfs=$SNAPCTR

#preparation for testing
log_must zfs snapshot $ctrfs@$TESTSNAP1
log_must zfs clone $ctrfs@$TESTSNAP1 $ctrclone
if is_global_zone; then
	log_must zfs create -V $VOLSIZE $ctrvol
else
	log_must zfs create $ctrvol
fi

log_must zfs snapshot -r $snappool
block_device_wait

#verify the snapshot -r results
for snap in $snappool $snapfs $snapvol $snapctr $snapctrvol \
		$snapctrclone $snapctrfs
do
	! snapexists $snap && \
		log_fail "The snapshot $snap is not created via -r option."
done

log_note "Verify that destroy -r can destroy the snapshot tree."

log_must zfs destroy -r $snappool
for snap in $snappool $snapfs $snapvol $snapctr $snapctrvol \
		$snapctrclone $snapctrfs
do
	snapexists $snap && \
		log_fail "The snapshot $snap is not destroyed correctly."
done

log_note "Verify that the snapshot with different name should \
		be not destroyed."
! snapexists $ctrfs@$TESTSNAP1 && \
	log_fail "destroy -r incorrectly destroys the snapshot \
		$ctrfs@$TESTSNAP1."

log_pass  "snapshot|destroy -r with snapshot tree works as expected."
