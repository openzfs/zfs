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

#
# DESCRIPTION:
#	A sequential rebuild does not copy from a mirror child that is missing
#	data while another child has it, and never reads the device it is
#	rebuilding.
#
# STRATEGY:
#	1. Write a file while one child of a two-way mirror is offline, then
#	   attach a new device with a sequential rebuild, held before it
#	   starts, and bring the stale child back. Its healing resilver is
#	   not started while the rebuild runs.
#	2. Delay reads of the complete child, so that the stale child has the
#	   shorter queue and would be chosen by load.
#	3. After the rebuild, detach both original children and verify the
#	   file cold from the new device.
#	4. Repeat with "zpool replace -s" of the complete child. The replacing
#	   vdev holds the complete copy and remains a source; the replacement
#	   must hold the file alone once the original is detached.
#	5. Repeat with every read of the complete child failing. The rebuild
#	   must not copy the stale child instead, which would also self-heal
#	   the complete child with its data: verify the file cold from the
#	   complete child alone.
#	6. Rebuild onto a new device while every read of the only source
#	   fails. The rebuild must report errors rather than read the device
#	   it is rebuilding, and the source must remain required.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	destroy_pool $TESTPOOL1
	set_tunable32 SCAN_SUSPEND_PROGRESS $orig_suspend
	set_tunable32 REBUILD_SCRUB_ENABLED $orig_scrub
	rm -rf $workdir
}

log_assert "A sequential rebuild does not copy from a stale mirror child"
orig_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS)
orig_scrub=$(get_tunable REBUILD_SCRUB_ENABLED)
workdir=$(mktemp -d $TEST_BASE_DIR/rebuild_stale_source.XXXXXX) ||
    log_fail "cannot create test directory"
log_onexit cleanup
# The verification scrub would repair the new device from a good copy.
log_must set_tunable32 REBUILD_SCRUB_ENABLED 0

log_must truncate -s 512M $workdir/disk-{0,1,2}
log_must zpool create -f $TESTPOOL1 mirror $workdir/disk-0 $workdir/disk-1
log_must zfs create -o compression=off $TESTPOOL1/$TESTFS
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must zpool offline $TESTPOOL1 $workdir/disk-1
log_must dd if=/dev/urandom of=$workdir/expected bs=1M count=64
log_must cp $workdir/expected $mntpnt/file
sync_pool $TESTPOOL1

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach -s $TESTPOOL1 $workdir/disk-0 $workdir/disk-2
log_must zpool online $TESTPOOL1 $workdir/disk-1
log_must zinject -d $workdir/disk-0 -D 20:1 -T read $TESTPOOL1
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver $TESTPOOL1
log_must zinject -c all

log_must zpool detach $TESTPOOL1 $workdir/disk-0
log_must zpool detach $TESTPOOL1 $workdir/disk-1
log_must zpool export $TESTPOOL1
log_must zpool import -d $workdir $TESTPOOL1
log_must cmp $workdir/expected $mntpnt/file
destroy_pool $TESTPOOL1

log_note "Replacement beside a stale child"
log_must truncate -s 0 $workdir/disk-{0,1,2}
log_must truncate -s 512M $workdir/disk-{0,1,2}
log_must zpool create -f $TESTPOOL1 mirror $workdir/disk-0 $workdir/disk-1
log_must zfs create -o compression=off $TESTPOOL1/$TESTFS
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must zpool offline $TESTPOOL1 $workdir/disk-1
log_must cp $workdir/expected $mntpnt/file
sync_pool $TESTPOOL1
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace -s $TESTPOOL1 $workdir/disk-0 $workdir/disk-2
log_must zpool online $TESTPOOL1 $workdir/disk-1
log_must zinject -d $workdir/disk-0 -D 20:1 -T read $TESTPOOL1
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver,replace $TESTPOOL1
log_must zinject -c all
log_must zpool detach $TESTPOOL1 $workdir/disk-1
log_must zpool export $TESTPOOL1
log_must zpool import -d $workdir $TESTPOOL1
log_must cmp $workdir/expected $mntpnt/file
destroy_pool $TESTPOOL1

log_note "Unreadable source beside a stale child"
log_must truncate -s 0 $workdir/disk-{0,1,2}
log_must truncate -s 512M $workdir/disk-{0,1,2}
log_must zpool create -f $TESTPOOL1 mirror $workdir/disk-0 $workdir/disk-1
log_must zfs create -o compression=off -o primarycache=metadata \
    $TESTPOOL1/$TESTFS
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must zpool offline $TESTPOOL1 $workdir/disk-1
log_must cp $workdir/expected $mntpnt/file
sync_pool $TESTPOOL1
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach -s $TESTPOOL1 $workdir/disk-0 $workdir/disk-2
log_must zpool online $TESTPOOL1 $workdir/disk-1
log_must zinject -d $workdir/disk-0 -e io -T read -f 100 $TESTPOOL1
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver $TESTPOOL1
log_must zinject -c all
log_must zpool detach $TESTPOOL1 $workdir/disk-1
log_must zpool detach $TESTPOOL1 $workdir/disk-2
log_must zpool export $TESTPOOL1
log_must zpool import -d $workdir $TESTPOOL1
log_must cmp $workdir/expected $mntpnt/file
destroy_pool $TESTPOOL1

log_note "Unreadable source"
log_must truncate -s 0 $workdir/disk-{0,1}
log_must truncate -s 512M $workdir/disk-{0,1}
log_must zpool create -f $TESTPOOL1 $workdir/disk-0
log_must zfs create -o compression=off -o primarycache=metadata \
    $TESTPOOL1/$TESTFS
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must cp $workdir/expected $mntpnt/file
sync_pool $TESTPOOL1
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach -s $TESTPOOL1 $workdir/disk-0 $workdir/disk-1
log_must zinject -d $workdir/disk-0 -e io -T read -f 100 $TESTPOOL1
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver $TESTPOOL1
log_must zinject -c all
# Reads of the new device's unwritten sectors must not complete the rebuild.
log_must check_pool_status $TESTPOOL1 "scan" "with [1-9][0-9]* errors" true
# The errors keep the new device's missing ranges, so the source remains
# required.
log_mustnot zpool detach $TESTPOOL1 $workdir/disk-0

log_pass "A sequential rebuild did not copy from a stale mirror child"
