#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
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
# Copyright (c) 2026, Gluesys. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION:
#	Verify that a single-disk pool correctly suspends when its device is
#	permanently removed (SCSI delete), rather than hanging in an infinite
#	zio reexecute loop.  Before the fix, WRITE zios that received ENXIO
#	at the root logical zio level were not covered by the suspend
#	condition in zio_done(), causing them to be infinitely reexecuted
#	instead of suspending the pool.  This resulted in sync and other
#	operations hanging permanently in D-state.
#
# STRATEGY:
#	1. Create a single-disk pool on a scsi_debug device
#	2. Write data to the pool (dirty data in ARC)
#	3. Permanently remove the device (echo 1 > device/delete)
#	4. Verify the pool transitions to SUSPENDED state
#	5. Bring the device back via SCSI rescan
#	6. zpool clear to resume the pool
#	7. Verify data integrity
#

DATAFILE=$(mktemp)

function cleanup
{
	if poolexists $TESTPOOL; then
		zpool clear $TESTPOOL 2>/dev/null
		destroy_pool $TESTPOOL 2>/dev/null
	fi
	unload_scsi_debug
	rm -f $DATAFILE
}

log_onexit cleanup

log_assert "single-disk pool suspends on permanent device removal (SCSI delete)"

# create test data
log_must dd if=/dev/urandom of=$DATAFILE bs=128K count=4
typeset sum1=$(xxh128digest $DATAFILE)

# create a scsi_debug device
load_scsi_debug 100 1 1 1 '512b'
sd=$(get_debug_device)
log_note "scsi_debug device: $sd"

# create a single-device pool and write some data
log_must zpool create $TESTPOOL $sd
log_must zpool sync

# copy data onto the pool (will be in ARC, not yet on disk)
log_must cp $DATAFILE /$TESTPOOL/file

# permanently remove the device via SCSI delete.  this causes all
# subsequent I/O to the device to fail.  the error reaches the root
# logical zio as ENXIO, which (with the fix) triggers pool suspension.
log_note "removing device $sd via SCSI delete"
log_must eval "echo 1 > /sys/block/$sd/device/delete"

# with the fix, the pool should suspend within a few seconds as
# pending writes fail and zio_done() triggers zio_suspend().  without
# the fix, WRITE ENXIO causes infinite zio reexecute and the pool
# stays ONLINE forever, with sync hung in D-state.
log_note "waiting for pool to suspend"
typeset -i tries=30
while [[ $(kstat_pool $TESTPOOL state) != "SUSPENDED" ]] ; do
	if ((tries-- == 0)); then
		log_fail "pool did not suspend after device removal"
	fi
	sleep 1
done
log_note "pool suspended successfully after device removal"

# bring the device back by rescanning all SCSI hosts.  the scsi_debug
# module is still loaded, so the virtual target will respond and the
# device will be recreated (possibly under a different name).
for h in /sys/class/scsi_host/host*; do
	echo "- - -" > $h/scan 2>/dev/null
done
block_device_wait

# clear the error state, which should reopen the vdev, resume the pool,
# and replay the failed I/O
log_must zpool clear $TESTPOOL

# wait for the pool to settle and complete the txg sync
log_note "waiting for pool to settle"
sleep 7

# verify the pool is back online
if [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]] ; then
	log_fail "pool still suspended after clear and device restore"
fi

# verify data integrity — the file written before the device was
# removed should be fully intact after the pool resumes and syncs
typeset sum2=$(xxh128digest /$TESTPOOL/file)
if [[ "$sum1" != "$sum2" ]] ; then
	log_fail "checksum mismatch: expected $sum1, got $sum2"
fi

log_pass "single-disk pool suspends on device removal and resumes cleanly"
