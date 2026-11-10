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
# Copyright (c) 2026 by Oxide Computer Company. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
# A scrub of a RAID-Z pool must use the hot spare's copy of a block when the
# spare's original device has come back but has not been resilvered yet.
# The original device's copy is stale, so a block that also has a damaged
# column on another device can only be reconstructed with the spare's copy.
#
# STRATEGY:
# 1. Create a raidz1 pool of three file vdevs and a hot spare, write a file,
#    replace the first vdev with the spare and let the resilver finish.
# 2. Offline the first vdev and write a second file, which the first vdev
#    misses.  Damage the second vdev outside its labels.
# 3. Delay the I/O of the pool's vdevs and start the sync of a txg.  While
#    it syncs, start a scrub and then online the first vdev, so that the
#    scrub runs instead of the first vdev's resilver.  This depends on
#    timing, so if the resilver runs instead, start over.
# 4. Check that the scrub found no data errors.
# 5. Detach the spare, so that the first vdev holds the only copy of its
#    column, re-import the pool and verify both files.
#

verify_runnable "global"

typeset VDEV_DIR=$TEST_BASE_DIR/scrub_spare_stale_original
typeset A=$VDEV_DIR/a
typeset B=$VDEV_DIR/b
typeset C=$VDEV_DIR/c
typeset S=$VDEV_DIR/s
typeset mntpnt digest1 digest2

function cleanup
{
	zinject -c all
	wait
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND_PROGRESS
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $VDEV_DIR
}

#
# Steps 1-3.  Return 0 if a scrub ran in place of the first vdev's resilver.
#
function scrub_instead_of_resilver
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $VDEV_DIR

	# 1. Create the pool, write a file and replace the first vdev
	log_must mkdir -p $VDEV_DIR
	log_must truncate -s $VDEV_FILE_SIZE $A $B $C $S
	log_must zpool create -f $TESTPOOL1 raidz1 $A $B $C spare $S
	mntpnt=$(get_prop mountpoint $TESTPOOL1)
	log_must dd if=/dev/urandom of=$mntpnt/file1 bs=1M count=32
	log_must zpool replace -w $TESTPOOL1 $A $S

	# 2. Write a file that the first vdev misses, and damage the second
	log_must zpool offline $TESTPOOL1 $A
	log_must dd if=/dev/urandom of=$mntpnt/file2 bs=1M count=32
	digest1=$(xxh128digest $mntpnt/file1)
	digest2=$(xxh128digest $mntpnt/file2)
	log_must zpool export $TESTPOOL1
	typeset size_mb=$(( $(stat_size $B) / 1024 / 1024 ))
	log_must dd if=/dev/urandom of=$B bs=1M seek=4 \
	    count=$((size_mb - 8)) conv=notrunc
	log_must zpool import -d $VDEV_DIR $TESTPOOL1

	#
	# 3. Start a scrub while a slow txg syncs, and online the first vdev
	# before the scrub's setup has synced.  Hold the scrub's I/O until the
	# delays are removed, so that it doesn't run under them.
	#
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	for vdev in $B $C $S; do
		log_must zinject -d $vdev -D 100:1 $TESTPOOL1
	done
	log_must dd if=/dev/urandom of=$mntpnt/file3 bs=1M count=1
	zpool sync $TESTPOOL1 &
	typeset sync_pid=$!
	sleep 0.5
	zpool scrub $TESTPOOL1 &
	typeset scrub_pid=$!
	sleep 0.3
	log_must zpool online $TESTPOOL1 $A
	wait $scrub_pid
	typeset scrub_rc=$?
	wait $sync_pid || log_fail "zpool sync $TESTPOOL1 failed"
	log_must zinject -c all
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must zpool wait -t scrub,resilver $TESTPOOL1

	log_note "$(zpool status $TESTPOOL1 | grep 'scan:')"
	(( scrub_rc == 0 )) && is_pool_scrubbed $TESTPOOL1
}

log_assert "A scrub uses a spare's copy while its original device is stale"

ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)
log_onexit cleanup

typeset -i tries=0
until scrub_instead_of_resilver; do
	(( ++tries < 5 )) || \
	    log_fail "The scrub never ran instead of the resilver"
	log_note "The resilver ran instead of the scrub; trying again"
done

# 4. Check that the scrub found no data errors
if ! check_pool_status $TESTPOOL1 "errors" "No known data errors"; then
	zpool status -v $TESTPOOL1
	log_fail "The scrub found data errors"
fi

# 5. Leave the first vdev as the only copy of its column and check the files
log_must zpool detach $TESTPOOL1 $S
log_must zpool export $TESTPOOL1
log_must zpool import -d $VDEV_DIR $TESTPOOL1
[[ $(xxh128digest $mntpnt/file1) == "$digest1" ]] || log_fail "file1 changed"
[[ $(xxh128digest $mntpnt/file2) == "$digest2" ]] || log_fail "file2 changed"
log_must zpool scrub -w $TESTPOOL1
if ! check_pool_status $TESTPOOL1 "errors" "No known data errors"; then
	zpool status -v $TESTPOOL1
	log_fail "$TESTPOOL1 has data errors"
fi

log_pass "A scrub uses a spare's copy while its original device is stale"
