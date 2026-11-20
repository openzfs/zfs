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
# 'zpool online' and 'zpool online -e' of the root vdev fail with
# "operation not supported on this type of pool", and leave the pool
# usable.
#
# zpool looks up a numeric device name as a vdev GUID, and the root
# vdev's GUID is the pool GUID, so 'zpool online POOL <pool GUID>' asks
# the kernel to online the root vdev.  vdev_online() then reopened the
# root vdev's vdev_top, which is NULL: the zpool process was killed by a
# NULL pointer dereference (a panic on FreeBSD), and the SCL_STATE_ALL
# config locks it held were never released, so the pool hung.
#
# STRATEGY:
# 1. Create a pool of one two-way mirror of file vdevs, and write a file.
# 2. Run 'zpool online' and 'zpool online -e' with the pool GUID.  Each
#    must exit 1 with the "operation not supported" error.
# 3. After each, the pool must still be ONLINE and take writes and syncs.
# 4. Offline and online a leaf, scrub, and export and import the pool:
#    nothing was left held, and the file reads back intact.
#

verify_runnable "global"

VDEV1=$TEST_BASE_DIR/zpool_online_003.1
VDEV2=$TEST_BASE_DIR/zpool_online_003.2
ERRFILE=$TEST_BASE_DIR/zpool_online_003.err
WEDGED=false

function cleanup
{
	# A killed 'zpool online' leaves the pool's config locks held, and
	# anything that touches the pool then hangs.  Leave it for the
	# reboot rather than hang the test run.
	if ! $WEDGED; then
		poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
		rm -f $VDEV1 $VDEV2
	fi
	rm -f $ERRFILE
}

log_assert "'zpool online' of the root vdev fails and leaves the pool usable"
log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2
log_must zpool create -f $TESTPOOL1 mirror $VDEV1 $VDEV2
MNTPNT=$(get_prop mountpoint $TESTPOOL1)
log_must dd if=/dev/urandom of=$MNTPNT/file bs=128k count=8
log_must zpool sync $TESTPOOL1
CKSUM=$(xxh128digest $MNTPNT/file)

POOLGUID=$(get_pool_prop guid $TESTPOOL1)
log_note "pool GUID $POOLGUID"

for opt in "" "-e"; do
	if [[ -z $opt ]]; then
		verb=online
	else
		verb=expand
	fi

	zpool online $opt $TESTPOOL1 $POOLGUID >$ERRFILE 2>&1
	ret=$?
	log_note "zpool online $opt $TESTPOOL1 $POOLGUID: exit $ret:" \
	    "$(<$ERRFILE)"
	# ksh reports a process killed by signal N as exit status 256 + N.
	if (( ret >= 256 )); then
		WEDGED=true
		log_fail "zpool online $opt with the pool GUID was killed" \
		    "(exit $ret)"
	fi
	(( ret == 1 )) || log_fail "zpool online $opt with the pool GUID" \
	    "exited $ret, expected 1"
	log_must grep -qx "cannot $verb $POOLGUID: operation not supported on this type of pool" $ERRFILE

	log_must check_state $TESTPOOL1 "" "ONLINE"
	log_must dd if=/dev/urandom of=$MNTPNT/file.$verb bs=128k count=1
	log_must zpool sync $TESTPOOL1
done

log_must zpool offline $TESTPOOL1 $VDEV1
log_must check_state $TESTPOOL1 $VDEV1 "OFFLINE"
log_must zpool online $TESTPOOL1 $VDEV1
log_must check_state $TESTPOOL1 $VDEV1 "ONLINE"
log_must zpool wait -t resilver $TESTPOOL1
log_must zpool scrub -w $TESTPOOL1
log_must check_state $TESTPOOL1 "" "ONLINE"
log_must zpool export $TESTPOOL1
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL1
log_must test "$(xxh128digest $MNTPNT/file)" = "$CKSUM"

log_pass "'zpool online' of the root vdev fails and leaves the pool usable"
