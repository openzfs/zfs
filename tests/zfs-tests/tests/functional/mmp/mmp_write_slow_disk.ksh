#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2024, Klara Inc
#

# DESCRIPTION:
#	Verify that long VDEV probes do not cause MMP checks to suspend pool
#	Note: without PR-15839 fix, this test will suspend the pool.
#
#	A device that is returning unexpected errors will trigger a vdev_probe.
#	When the device additionally has slow response times, the probe can hold
#	the spa config lock as a writer for a long period of time such that the
#	mmp uberblock updates stall when trying to acquire the spa config lock.
#
# STRATEGY:
#	1. Create a pool with multiple leaf vdevs
#	2. Enable multihost and multihost_history
#	3. Delay for MMP writes to occur
#	4. Verify that a long VDEV probe didn't cause MMP check to suspend pool
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

function cleanup
{
	log_must zinject -c all

	if [[ $(zpool list -H -o health $MMP_POOL) == "SUSPENDED" ]]; then
		log_must zpool clear $MMP_POOL
		zpool get state $MMP_POOL $MMP_DIR/file.3
		zpool events | grep ".fs.zfs." | grep -v "history_event"
	fi

	poolexists $MMP_POOL && destroy_pool $MMP_POOL
	log_must rm -r $MMP_DIR
	log_must mmp_clear_hostid
}

log_assert "A long VDEV probe doesn't cause a MMP check suspend"
log_onexit cleanup

MMP_HISTORY_TMP=$MMP_DIR/history

# Create a multiple drive pool
log_must zpool events -c
log_must mkdir -p $MMP_DIR
log_must truncate -s 128M $MMP_DIR/file.{0,1,2,3,4,5}
log_must zpool create -f $MMP_POOL \
	mirror $MMP_DIR/file.{0,1,2} \
	mirror $MMP_DIR/file.{3,4,5}

# Enable MMP
log_must mmp_set_hostid $HOSTID1
log_must zpool set multihost=on $MMP_POOL
clear_mmp_history

# Inject vdev write error along with a delay
log_must zinject -f 33 -e io -L pad2 -T write -d $MMP_DIR/file.3 $MMP_POOL
log_must zinject -f 50 -e io -L uber -T write -d $MMP_DIR/file.3 $MMP_POOL
log_must zinject -D 2000:4 -T write -d $MMP_DIR/file.3 $MMP_POOL

log_must dd if=/dev/urandom of=/$MMP_POOL/data bs=1M count=5
sleep 10
sync_pool $MMP_POOL

# Confirm mmp writes to the non-slow disks have taken place
kstat_pool $MMP_POOL multihost > $MMP_HISTORY_TMP
for x in {0,1,2,4}; do
	write_count=$(grep -c file.${x} $MMP_HISTORY_TMP)
	[[ $write_count -gt 0 ]] || log_fail "expecting mmp writes"
done

# Expect that the pool was not suspended
log_must check_state $MMP_POOL "" "ONLINE"
health=$(zpool list -H -o health $MMP_POOL)
log_note "$MMP_POOL health is $health"
[[ "$health" == "SUSPENDED" ]] && log_fail "$MMP_POOL $health unexpected"

log_pass "A long VDEV probe doesn't cause a MMP check suspend"
