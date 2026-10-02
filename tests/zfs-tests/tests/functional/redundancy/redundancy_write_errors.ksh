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
#	A failed write to a child of a mirror or RAID-Z vdev is recorded as
#	missing data on that child, even when the write fails on its first
#	attempt and redundancy absorbs it.
#
# STRATEGY:
#	1. Fail every write to one child of a mirror with failfast errors,
#	   which are not retried, while writing a file.
#	2. Require that the child cannot be detached from, and that a
#	   healing resilver repairs it: detach the other child and verify the
#	   file cold.
#	3. Repeat for the repair writes of a resilver onto an attached device.
#	4. Fail the first attempt of every write to both children, so that
#	   each write succeeds when it is retried. The failures are recorded
#	   on both children, and healing clears them.
#	5. Fail every write to one child of a RAID-Z1 vdev. Parity absorbs the
#	   failures, which are recorded and healed.
#	6. Fail every write of a sequential rebuild onto an attached device.
#	   Recording those failures must keep the rebuild's TXG bounds valid
#	   until it completes.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	destroy_pool $TESTPOOL1
	set_tunable32 SCAN_SUSPEND_PROGRESS $orig_suspend
	rm -rf $workdir
}

# Whether the leaf's synced DTL records missing data.
function missing
{
	typeset -i rc

	sync_pool $TESTPOOL1
	zdb -ddd $TESTPOOL1 | awk -v leaf=$1 '
	    $2 ~ /^\[DTL-/ { selected = ($1 == leaf); seen += selected }
	    selected && $1 == "missing" { found = 1 }
	    END { exit !seen ? 2 : !found }'
	rc=$?
	(( rc == 2 )) && log_fail "zdb listed no DTL of $1"
	return $rc
}

# Heal, then read the file from the given device alone.
function verify_healed
{
	typeset healed=$1 other=$2
	typeset -i i
	# Reopening requests the healing pass asynchronously.
	log_must zpool reopen $TESTPOOL1
	for (( i = 0; i < 60; i++ )); do
		log_must zpool wait -t resilver $TESTPOOL1
		missing $healed || break
		sleep 1
	done
	(( i < 60 )) || log_fail "missing data on $healed was not healed"
	log_must zpool detach $TESTPOOL1 $other
	log_must zpool export $TESTPOOL1
	log_must zpool import -d $workdir $TESTPOOL1
	log_must cmp $workdir/expected $mntpnt/file
	log_must zpool scrub -w $TESTPOOL1
	log_must check_pool_status $TESTPOOL1 "scan" "with 0 errors" true
	destroy_pool $TESTPOOL1
}

log_assert "Write failures absorbed by redundancy are recorded"
orig_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS)
workdir=$(mktemp -d $TEST_BASE_DIR/redundancy_write_errors.XXXXXX) ||
    log_fail "cannot create test directory"
log_onexit cleanup
log_must dd if=/dev/urandom of=$workdir/expected bs=1M count=32

log_note "Ordinary writes"
log_must truncate -s 512M $workdir/disk-{0,1}
log_must zpool create -f $TESTPOOL1 mirror $workdir/disk-0 $workdir/disk-1
log_must zfs create -o compression=off $TESTPOOL1/$TESTFS
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
# Drop the data as well as failing the write.
log_must zinject -F -d $workdir/disk-1 -e noop -T write -f 100 $TESTPOOL1
log_must zinject -F -d $workdir/disk-1 -e io -T write -f 100 $TESTPOOL1
log_must cp $workdir/expected $mntpnt/file
sync_pool $TESTPOOL1
log_must zinject -c all
log_must check_state $TESTPOOL1 $workdir/disk-1 online
# The only complete copy of the file must not be detachable.
log_mustnot zpool detach $TESTPOOL1 $workdir/disk-0
verify_healed $workdir/disk-1 $workdir/disk-0

log_note "Resilver repair writes"
log_must truncate -s 0 $workdir/disk-{0,1}
log_must truncate -s 512M $workdir/disk-{0,1}
log_must zpool create -f $TESTPOOL1 $workdir/disk-0
log_must zfs create -o compression=off $TESTPOOL1/$TESTFS
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must cp $workdir/expected $mntpnt/file
sync_pool $TESTPOOL1
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach $TESTPOOL1 $workdir/disk-0 $workdir/disk-1
log_must zinject -F -d $workdir/disk-1 -e noop -T write -f 100 $TESTPOOL1
log_must zinject -F -d $workdir/disk-1 -e io -T write -f 100 $TESTPOOL1
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver $TESTPOOL1
log_must zinject -c all
log_must check_state $TESTPOOL1 $workdir/disk-1 online
log_mustnot zpool detach $TESTPOOL1 $workdir/disk-0
verify_healed $workdir/disk-1 $workdir/disk-0

log_note "Writes which succeed when retried"
log_must truncate -s 0 $workdir/disk-{0,1}
log_must truncate -s 512M $workdir/disk-{0,1}
log_must zpool create -f $TESTPOOL1 mirror $workdir/disk-0 $workdir/disk-1
log_must zfs create -o compression=off $TESTPOOL1/$TESTFS
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
# Failfast injection does not match the retry, which succeeds.
log_must zinject -F -d $workdir/disk-0 -e io -T write -f 100 $TESTPOOL1
log_must zinject -F -d $workdir/disk-1 -e io -T write -f 100 $TESTPOOL1
log_must cp $workdir/expected $mntpnt/file
sync_pool $TESTPOOL1
log_must zinject -c all
log_must missing $workdir/disk-0
log_must missing $workdir/disk-1
verify_healed $workdir/disk-1 $workdir/disk-0

log_note "RAID-Z writes"
log_must truncate -s 0 $workdir/disk-{0,1,2}
log_must truncate -s 512M $workdir/disk-{0,1,2}
log_must zpool create -f $TESTPOOL1 raidz1 $workdir/disk-{0,1,2}
log_must zfs create -o compression=off $TESTPOOL1/$TESTFS
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must zinject -F -d $workdir/disk-2 -e noop -T write -f 100 $TESTPOOL1
log_must zinject -F -d $workdir/disk-2 -e io -T write -f 100 $TESTPOOL1
log_must cp $workdir/expected $mntpnt/file
sync_pool $TESTPOOL1
log_must zinject -c all
log_must check_state $TESTPOOL1 $workdir/disk-2 online
log_must missing $workdir/disk-2
log_must zpool reopen $TESTPOOL1
typeset -i i
for (( i = 0; i < 60; i++ )); do
	log_must zpool wait -t resilver $TESTPOOL1
	missing $workdir/disk-2 || break
	sleep 1
done
(( i < 60 )) || log_fail "missing data on disk-2 was not healed"
# Read the file with disk-2 in place of another child.
log_must zpool offline $TESTPOOL1 $workdir/disk-0
log_must zpool export $TESTPOOL1
log_must zpool import -d $workdir $TESTPOOL1
log_must cmp $workdir/expected $mntpnt/file
destroy_pool $TESTPOOL1

log_note "Sequential rebuild writes"
log_must truncate -s 0 $workdir/disk-{0,1}
log_must truncate -s 512M $workdir/disk-{0,1}
log_must zpool create -f $TESTPOOL1 $workdir/disk-0
log_must zfs create -o compression=off $TESTPOOL1/$TESTFS
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must cp $workdir/expected $mntpnt/file
# Span several metaslabs, each of which rechecks the rebuild's bounds.
log_must dd if=/dev/urandom of=$mntpnt/filler bs=1M count=160
sync_pool $TESTPOOL1
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach -s $TESTPOOL1 $workdir/disk-0 $workdir/disk-1
log_must zinject -F -d $workdir/disk-1 -e noop -T write -f 100 $TESTPOOL1
log_must zinject -F -d $workdir/disk-1 -e io -T write -f 100 $TESTPOOL1
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver $TESTPOOL1
log_must zinject -c all
log_must eval "zpool history -i $TESTPOOL1 | grep -q ' rebuild .* complete'"
destroy_pool $TESTPOOL1

log_pass "Write failures absorbed by redundancy were recorded"
