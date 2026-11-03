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
#	On a pool without feature@async_destroy, destroying a file system or
#	a volume, or rolling a file system back, frees the intent log even
#	when it is not claimed.  The header of a log written since the
#	import, or reset by the replay that a mount or a volume's minor does,
#	holds a block but no claim txg.
#
# STRATEGY:
#	1. Create a pool with feature@async_destroy disabled
#	2. Create a file system and a volume with sync=always and write to
#	   each, then destroy them in the same import
#	3. Create and write them again, export and import the pool, which
#	   mounts the file system and creates the volume's minor, then
#	   destroy them
#	4. Snapshot a file system, write to it and roll it back
#	5. After each destroy and the rollback, check that zdb -b finds no
#	   leaks
#

verify_runnable "global"

VDEV_DIR=$TEST_BASE_DIR/zfs_destroy_sync_zil
FS=$TESTPOOL2/fs
VOL=$TESTPOOL2/vol
ZDEV=$ZVOL_DEVDIR/$VOL

function cleanup
{
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	rm -rf $VDEV_DIR
}

function check_no_leaks # after
{
	typeset out

	log_must sync_pool $TESTPOOL2
	out=$(zdb -b $TESTPOOL2 2>&1) || log_fail "zdb -b after $1: $out"
	[[ $out == *"No leaks"* ]] || log_fail "zdb -b after $1: $out"
}

# An import creates a volume's minor asynchronously.
function wait_for_zdev
{
	typeset -i i

	for ((i = 0; i < 30; i++)); do
		block_device_wait $ZDEV
		is_disk_device $ZDEV && return
		sleep 1
	done
	log_fail "$ZDEV did not appear"
}

# sync=always sends every write through the log.
function create_and_write
{
	log_must zfs create -o sync=always $FS
	log_must zfs create -V 16m -o sync=always $VOL
	wait_for_zdev
	log_must dd if=/dev/urandom of=/$FS/file bs=128k count=8
	log_must dd if=/dev/urandom of=$ZDEV bs=128k count=8
}

log_assert "A synchronous destroy frees an unclaimed intent log."
log_onexit cleanup

log_must mkdir -p $VDEV_DIR
log_must truncate -s $((MINVDEVSIZE * 4)) $VDEV_DIR/vdev
log_must zpool create -o feature@async_destroy=disabled $TESTPOOL2 \
    $VDEV_DIR/vdev
[[ $(get_pool_prop feature@async_destroy $TESTPOOL2) == "disabled" ]] || \
    log_fail "feature@async_destroy is not disabled"

create_and_write
check_no_leaks "the writes"
destroy_dataset $FS
check_no_leaks "destroying the file system"
destroy_dataset $VOL
check_no_leaks "destroying the volume"

create_and_write
log_must_busy zpool export $TESTPOOL2
log_must zpool import -d $VDEV_DIR $TESTPOOL2
wait_for_zdev
destroy_dataset $FS
check_no_leaks "destroying the file system after an import"
destroy_dataset $VOL
check_no_leaks "destroying the volume after an import"

log_must zfs create -o sync=always $FS
log_must zfs snapshot $FS@snap
log_must dd if=/dev/urandom of=/$FS/file bs=128k count=8
log_must zfs rollback $FS@snap
check_no_leaks "the rollback"

log_pass "A synchronous destroy frees an unclaimed intent log."
