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
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	A block cloned and then freed in the same txg, while a write of the
#	same block from the previous txg is still syncing, reads back as a
#	hole.
#
# STRATEGY:
#	1. Write a source and a destination file and sync them, and check
#	   that copy_file_range(2) clones the source's first block.
#	2. Slow down the pool's writes, overwrite the destination's first
#	   block and start a txg sync, so that the write stays in flight.
#	3. In the next txg, clone the source's first block over it and
#	   truncate the destination to 0 and back, freeing the clone.
#	4. Read the destination's first block: it must read as zeros, not
#	   fail with EIO.
#	5. Require that the previous txg was still syncing during the read
#	   at least once, so that the race was exercised.
#

verify_runnable "global"

claim="A cloned block freed while its previous write syncs reads as a hole"

sync_pid=

function cleanup
{
	zinject -c all > /dev/null 2>&1
	[[ -n $sync_pid ]] && wait $sync_pid
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $TEST_BASE_DIR/zero.$$ $TEST_BASE_DIR/blk0.$$
}

log_onexit cleanup

log_assert $claim

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $DISKS
log_must zfs set recordsize=128k compression=off $TESTPOOL

MNT=/$TESTPOOL
log_must dd if=/dev/urandom of=$MNT/src bs=128k count=2
log_must dd if=/dev/urandom of=$MNT/dst bs=128k count=2
log_must sync_pool $TESTPOOL
log_must dd if=/dev/zero of=$TEST_BASE_DIR/zero.$$ bs=128k count=1

log_must clonefile -f $MNT/src $MNT/probe 0 0 131072
log_must sync_pool $TESTPOOL
log_must [ "$(get_same_blocks $TESTPOOL src $TESTPOOL probe)" = "0" ]

typeset -i exercised=0
for i in 1 2 3; do
	for disk in $DISKS; do
		log_must zinject -d $disk -D 2000:1 -T write $TESTPOOL
	done

	# txg N: a write of the first block that stays in flight
	log_must dd if=/dev/urandom of=$MNT/dst bs=128k count=1 conv=notrunc
	zpool sync $TESTPOOL &
	sync_pid=$!
	sleep 0.5

	# txg N+1: clone over that block and free it again
	log_must clonefile -f $MNT/src $MNT/dst 0 0 131072
	log_must truncate -s 0 $MNT/dst
	log_must truncate -s 262144 $MNT/dst

	log_must dd if=$MNT/dst of=$TEST_BASE_DIR/blk0.$$ bs=128k count=1
	log_must cmp $TEST_BASE_DIR/zero.$$ $TEST_BASE_DIR/blk0.$$
	if kill -0 $sync_pid 2> /dev/null; then
		exercised=exercised+1
	else
		log_note "iteration $i: the previous txg synced too early"
	fi

	log_must zinject -c all
	wait $sync_pid
	sync_pid=
	log_must sync_pool $TESTPOOL
	log_must dd if=/dev/urandom of=$MNT/dst bs=128k count=2 conv=notrunc
	log_must sync_pool $TESTPOOL
done

log_must [ $exercised -gt 0 ]

log_pass $claim
