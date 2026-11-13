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
# 'zpool detach' of the root vdev fails with "only applicable to mirror
# and replacing vdevs", and leaves the pool usable.
#
# zpool looks up a numeric device name as a vdev GUID, and the root
# vdev's GUID is the pool GUID, so 'zpool detach POOL <pool GUID>' asks
# the kernel to detach the root vdev.  spa_vdev_detach_enter() then
# passed the root vdev's vdev_top, which is NULL, to
# vdev_rebuild_stop_wait(): the zpool process was killed by a NULL
# pointer dereference (a panic on FreeBSD) while it held
# spa_namespace_lock and spa_vdev_top_lock, so every pool hung.
#
# STRATEGY:
# 1. Create a pool of one two-way mirror of file vdevs, and write a file.
# 2. Run 'zpool detach' with the pool GUID.  It must exit 255 (as for
#    any detach error) with the "only applicable to mirror and replacing
#    vdevs" error.
# 3. The pool must still be ONLINE and take writes and syncs.
# 4. Attach a third leaf, then detach it, and export and import the
#    pool: nothing was left held, and the file reads back intact.
#

verify_runnable "global"

VDEV1=$TEST_BASE_DIR/zpool_detach_002.1
VDEV2=$TEST_BASE_DIR/zpool_detach_002.2
VDEV3=$TEST_BASE_DIR/zpool_detach_002.3
ERRFILE=$TEST_BASE_DIR/zpool_detach_002.err
WEDGED=false

function cleanup
{
	# A killed 'zpool detach' leaves the pool namespace lock held, and
	# any pool command then hangs.  Leave it for the reboot rather than
	# hang the test run.
	if ! $WEDGED; then
		poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
		rm -f $VDEV1 $VDEV2 $VDEV3
	fi
	rm -f $ERRFILE
}

log_assert "'zpool detach' of the root vdev fails and leaves the pool usable"
log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2 $VDEV3
log_must zpool create -f $TESTPOOL1 mirror $VDEV1 $VDEV2
MNTPNT=$(get_prop mountpoint $TESTPOOL1)
log_must dd if=/dev/urandom of=$MNTPNT/file bs=128k count=8
log_must zpool sync $TESTPOOL1
CKSUM=$(xxh128digest $MNTPNT/file)

POOLGUID=$(get_pool_prop guid $TESTPOOL1)
log_note "pool GUID $POOLGUID"

zpool detach $TESTPOOL1 $POOLGUID >$ERRFILE 2>&1
ret=$?
log_note "zpool detach $TESTPOOL1 $POOLGUID: exit $ret: $(<$ERRFILE)"
# ksh reports a process killed by signal N as exit status 256 + N.
if (( ret >= 256 )); then
	WEDGED=true
	log_fail "zpool detach with the pool GUID was killed (exit $ret)"
fi
# zpool detach exits 255 on every error, as for any other non-leaf vdev.
(( ret == 255 )) || log_fail "zpool detach with the pool GUID exited" \
    "$ret, expected 255"
log_must grep -qx "cannot detach $POOLGUID: only applicable to mirror and replacing vdevs" $ERRFILE

log_must check_state $TESTPOOL1 "" "ONLINE"
log_must check_state $TESTPOOL1 $VDEV1 "ONLINE"
log_must check_state $TESTPOOL1 $VDEV2 "ONLINE"
log_must dd if=/dev/urandom of=$MNTPNT/file.2 bs=128k count=1
log_must zpool sync $TESTPOOL1

log_must zpool attach -w $TESTPOOL1 $VDEV1 $VDEV3
log_must check_state $TESTPOOL1 $VDEV3 "ONLINE"
log_must zpool detach $TESTPOOL1 $VDEV3
log_mustnot check_state $TESTPOOL1 $VDEV3 "ONLINE"
log_must check_state $TESTPOOL1 "" "ONLINE"
log_must zpool export $TESTPOOL1
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL1
log_must test "$(xxh128digest $MNTPNT/file)" = "$CKSUM"

log_pass "'zpool detach' of the root vdev fails and leaves the pool usable"
