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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_error_scrub.kshlib

#
# DESCRIPTION:
#	Verify a pool can be exported while an error scrub is in progress,
#	or after an error scrub was replaced by a resilver, and that an
#	error scrub resumed after import completes correctly.
#
# STRATEGY:
#	1. Create a pool and a file in it, and create permanent errors
#	   for the file.
#	2. Suspend scan progress and start an error scrub.
#	3. Export the pool, and verify the export completes.
#	4. Import the pool, and verify the error scrub is still in progress
#	   and the errors are still reported.
#	5. Resume scan progress, wait for the error scrub to finish, and
#	   verify it examined the error blocks and cleared the errors.
#	6. Create permanent errors again, suspend scan progress and start
#	   an error scrub.
#	7. Attach a device, which starts a resilver replacing the error
#	   scrub.
#	8. Export the pool, and verify the export completes.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	zinject -c all
	destroy_pool $TESTPOOL2
	rm -f $TESTDIR/vdev_a $TESTDIR/vdev_b
}

log_onexit cleanup

log_assert "Verify a pool can be exported during an error scrub."

truncate -s $MINVDEVSIZE $TESTDIR/vdev_a $TESTDIR/vdev_b
log_must zpool create -f -O primarycache=none $TESTPOOL2 $TESTDIR/vdev_a
log_must zfs create -o compression=off $TESTPOOL2/$TESTFS1
typeset file=/$TESTPOOL2/$TESTFS1/$TESTFILE0
log_must dd if=/dev/urandom of=$file bs=1M count=4
log_must sync_pool $TESTPOOL2

# Export during an in-progress error scrub.
create_permanent_errors $TESTPOOL2 $file

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool scrub -e $TESTPOOL2
log_must is_pool_error_scrubbing $TESTPOOL2 true

export_with_deadline $TESTPOOL2
log_must zpool import -d $TESTDIR $TESTPOOL2

log_must is_pool_error_scrubbing $TESTPOOL2 true
log_mustnot is_pool_error_scrubbed $TESTPOOL2
log_must eval "zpool status -v $TESTPOOL2 | grep '$file'"

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
wait_error_scrubbed $TESTPOOL2
log_must zpool status -v $TESTPOOL2
log_mustnot eval "zpool status -v $TESTPOOL2 | grep 'scrubbed 0 error blocks'"
log_mustnot eval "zpool status -v $TESTPOOL2 | grep '$file'"

# Export after an in-progress error scrub was replaced by a resilver.
create_permanent_errors $TESTPOOL2 $file

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool scrub -e $TESTPOOL2
log_must is_pool_error_scrubbing $TESTPOOL2 true

log_must zpool attach $TESTPOOL2 $TESTDIR/vdev_a $TESTDIR/vdev_b
typeset -i timer
for ((timer = 0; timer < 30; timer++)); do
	is_pool_resilvering $TESTPOOL2 && break
	sleep 1
done
log_must is_pool_resilvering $TESTPOOL2
log_mustnot is_pool_error_scrubbing $TESTPOOL2

export_with_deadline $TESTPOOL2
log_must zpool import -d $TESTDIR $TESTPOOL2

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver $TESTPOOL2
log_must check_state $TESTPOOL2 "" "online"

log_pass "Verified a pool can be exported during an error scrub."
