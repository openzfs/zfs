#! /bin/ksh -p
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
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
#	A removal is canceled while a child of the mirror that receives the
#	data can't be written, here because it is offline or faulted.  If
#	the removal completed, that child would never get the copied blocks:
#	they keep their birth txg, which the resilver of the child, once it
#	is back, doesn't cover.
#
# STRATEGY:
#	1. Create a pool with one disk, write data to it, and add a mirror.
#	2. Offline a child of the mirror and remove the disk.  Verify that
#	   the removal is canceled, then online the child.
#	3. Remove the disk again, fault the same child while the removal is
#	   paused, and verify that the removal is canceled.  Clear the fault.
#	4. Remove the disk with both children of the mirror writable.
#	5. Detach the other child, and verify that the child that was offline
#	   and faulted holds the data on its own.
#

verify_runnable "global"

DISKDIR=$(mktemp -d)
DISK0=$DISKDIR/dsk0
DISK1=$DISKDIR/dsk1
DISK2=$DISKDIR/dsk2

function cleanup
{
	log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 0
	default_cleanup_noexit
	log_must rm -rf $DISKDIR
}

function wait_for_removal_canceled # pool
{
	typeset pool=$1

	log_must zpool wait -t remove $pool
	sync_pool $pool
	log_must check_pool_status $pool "remove" "canceled on"
	log_must vdevs_in_pool $pool $DISK0
}

log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $DISK0
log_must truncate -s $((MINVDEVSIZE * 2)) $DISK1 $DISK2

log_must zpool create -f $TESTPOOL $DISK0
log_must zfs set compression=off $TESTPOOL
log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=1M count=64
log_must zpool sync $TESTPOOL
typeset cksum=$(xxh128digest /$TESTPOOL/file)
log_must zpool add -f $TESTPOOL mirror $DISK1 $DISK2

# A child of the mirror is offline when the removal starts.
log_must zpool offline $TESTPOOL $DISK2
log_must zpool remove $TESTPOOL $DISK0
wait_for_removal_canceled $TESTPOOL
log_must zpool online $TESTPOOL $DISK2
log_must zpool wait -t resilver $TESTPOOL

# A child of the mirror is faulted while the removal is in progress.
log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 1
log_must zpool remove $TESTPOOL $DISK0
sync_pool $TESTPOOL
log_must is_pool_removing $TESTPOOL
log_must zpool offline -f $TESTPOOL $DISK2
log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 0
wait_for_removal_canceled $TESTPOOL
log_must zpool clear $TESTPOOL $DISK2

log_must zpool remove -w $TESTPOOL $DISK0
sync_pool $TESTPOOL
log_must is_pool_removed $TESTPOOL
log_mustnot vdevs_in_pool $TESTPOOL $DISK0

# The resilver started by clearing the fault must be done for the detach.
log_must zpool wait -t resilver $TESTPOOL
log_must zpool detach $TESTPOOL $DISK1
log_must zpool export $TESTPOOL
log_must zpool import -d $DISKDIR $TESTPOOL
log_must [ "$(xxh128digest /$TESTPOOL/file)" = "$cksum" ]
verify_pool $TESTPOOL

log_pass "Removal is canceled while a destination mirror child is unwritable"
