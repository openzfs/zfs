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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_split/zpool_split.cfg

#
# DESCRIPTION:
#	'zpool split' works on a pool whose last top-level vdev is a log
#	that the log space maps hold entries for.  The new pool leaves the
#	log out altogether, as it is the last top-level vdev, but inherits
#	the log space maps, whose entries name the log by its vdev id:
#	loading the new pool must skip them, in the split and at any later
#	import.
#
# STRATEGY:
#	1. Create a mirrored pool whose last top-level vdev is a mirrored
#	   log, and keep the log space maps at export
#	2. Write with sync=always, so that log blocks are allocated and
#	   freed on the log, and verify with zdb that the log space maps
#	   hold entries for it
#	3. Split the pool; the new pool is exported with the log space
#	   maps it inherited
#	4. Import the new pool, verify the data written and scrub it
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL2
	destroy_pool $TESTPOOL
	restore_tunable KEEP_LOG_SPACEMAPS_AT_EXPORT
	rm -rf $VDEV_DIR
	rm -fd $altroot
}

log_assert "'zpool split' works on a pool whose log space maps hold" \
	"entries for its last top-level vdev, a log"
log_onexit cleanup

typeset VDEV_DIR="$TEST_BASE_DIR/zpool_split_log_spacemap"
typeset altroot="$TESTDIR/altroot-$TESTPOOL2"

log_must mkdir -p $VDEV_DIR
for vdev in data1 data2 log1 log2; do
	log_must truncate -s $SPA_MINDEVSIZE $VDEV_DIR/$vdev
done

log_must save_tunable KEEP_LOG_SPACEMAPS_AT_EXPORT
log_must set_tunable32 KEEP_LOG_SPACEMAPS_AT_EXPORT 1

# 1. The log is vdev 1, the last top-level vdev
log_must zpool create -f -O sync=always $TESTPOOL \
	mirror $VDEV_DIR/data1 $VDEV_DIR/data2 \
	log mirror $VDEV_DIR/log1 $VDEV_DIR/log2

# 2. Every write commits a log block on the log
typeset mntpnt digest
typeset -i entries
mntpnt=$(get_prop mountpoint $TESTPOOL)
log_must dd if=/dev/urandom of=$mntpnt/file bs=128k count=32
sync_pool $TESTPOOL
digest=$(xxh128digest $mntpnt/file)

entries=$(zdb -mmmm $TESTPOOL | grep -c ' vdev: 1$')
log_note "The log space maps hold $entries entries for the log"
log_must test $entries -gt 0

# 3. Without -R, the new pool is exported after the split
log_must zpool split $TESTPOOL $TESTPOOL2
log_mustnot poolexists $TESTPOOL2

# 4. The import reads the log space maps that the new pool kept
log_must zpool import -d $VDEV_DIR -R $altroot $TESTPOOL2
mntpnt=$(get_prop mountpoint $TESTPOOL2)
log_must test "$(xxh128digest $mntpnt/file)" = "$digest"
log_must zpool scrub -w $TESTPOOL2
log_must check_pool_status $TESTPOOL2 "errors" "No known data errors"

log_pass "'zpool split' works on a pool whose log space maps hold" \
	"entries for its last top-level vdev, a log"
