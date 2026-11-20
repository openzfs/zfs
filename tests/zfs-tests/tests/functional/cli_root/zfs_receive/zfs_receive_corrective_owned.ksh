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
# A corrective receive into a snapshot that is mounted fails with "dataset
#   is busy" and the pool keeps syncing, and a corrective receive does not
#   leave the snapshot marked inconsistent, whether it fails or succeeds.
#
# STRATEGY:
# 1. Create a filesystem with a file, snapshot it and send the snapshot to a
#    file
# 2. Heal the snapshot from a truncated copy of the stream, and check that
#    the receive fails and leaves the snapshot consistent
# 3. Heal the snapshot from the whole stream, and check that the receive
#    succeeds, leaves the snapshot consistent and that the snapshot can be
#    mounted through .zfs/snapshot
# 4. Hold a file open in the mounted snapshot and heal it again, and check
#    that the receive fails with "dataset is busy", that the pool still
#    syncs and that the snapshot is still consistent
#

verify_runnable "global"

typeset fs=$TESTPOOL/$TESTFS1
typeset snap=$fs@snap1
typeset backup=$TEST_BASE_DIR/corrective_owned.zsend
typeset truncated=$TEST_BASE_DIR/corrective_owned_truncated.zsend

function cleanup
{
	exec 3<&-
	datasetexists $fs && destroy_dataset $fs -r
	rm -f $backup $truncated
}

function check_consistent # snapshot
{
	typeset val=$(get_prop inconsistent $1)

	[[ "$val" == "0" ]] || log_fail "$1 is marked inconsistent ($val)"
}

log_assert "Corrective receive fails cleanly on an owned snapshot and" \
    "leaves the snapshot consistent"
log_onexit cleanup

log_must zfs create $fs
typeset mntpnt=$(get_prop mountpoint $fs)
log_must dd if=/dev/urandom of=$mntpnt/$TESTFILE0 bs=128k count=32
log_must zfs snapshot $snap
log_must eval "zfs send $snap > $backup"
typeset snapdir=$(snapshot_mountpoint $snap)

# A corrective receive that fails part way leaves the snapshot consistent
log_must dd if=$backup of=$truncated bs=128k count=8
log_mustnot eval "zfs receive -c $snap < $truncated"
check_consistent $snap

# So does one that succeeds, and the snapshot can still be mounted
log_must eval "zfs receive -c $snap < $backup"
check_consistent $snap
log_must cmp $mntpnt/$TESTFILE0 $snapdir/$TESTFILE0

# The mount owns the snapshot, and the open file keeps it owned
exec 3< $snapdir/$TESTFILE0
typeset out
out=$(zfs receive -c $snap < $backup 2>&1) && \
    log_fail "corrective receive into a mounted snapshot succeeded"
[[ "$out" == *"dataset is busy"* ]] || \
    log_fail "corrective receive into a mounted snapshot failed with: $out"
log_note "corrective receive into a mounted snapshot failed: $out"
sync_pool $TESTPOOL
check_consistent $snap
exec 3<&-

log_pass "Corrective receive fails cleanly on an owned snapshot and" \
    "leaves the snapshot consistent"
