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
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib

#
# DESCRIPTION:
#	zfs set defaultuserquota takes a filesystem's teardown lock as
#	writer.  It must finish while an apply batch holds two files
#	of that filesystem, with the surviving copy in the same
#	filesystem or in another mounted one.  On FreeBSD zfs_enter()
#	waits behind a waiting writer even in a thread that has
#	entered already, so a worker that entered once for each file
#	never got its second entry, and the writer waited for its
#	first.  Linux lets the second entry through, so there the test
#	passes either way.
#
# STRATEGY:
#	1. Write a file and two byte-for-byte copies of it in one
#	   mounted filesystem.
#	2. Apply on one worker, two blocks to a batch and a second
#	   before each block, so every batch holds one copy open while
#	   it opens the other.
#	3. While the apply runs, set defaultuserquota again and again,
#	   and require each set to return within a minute.
#	4. Check that the run was still applying after the last set,
#	   and that its batches opened two files each.
#	5. Let the run finish, and check that the copies share the
#	   file's blocks.
#	6. Do it again on a new pool, with the file in one mounted
#	   filesystem and its two later copies in another, setting
#	   the property on both filesystems in turn.  Finish a run
#	   before making the copies, so the file is their source.
#

verify_runnable "global"

typeset -ri nblk=24
typeset -ri nsets=8

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	log_must restore_tunable CLONEDUP_APPLY_THREADS
	log_must restore_tunable TXG_TIMEOUT
	clonedup_cleanup
}

#
# Run a slow apply, set defaultuserquota nsets times on the given
# filesystems in turn while it runs, then let it finish.  Each set
# must return within a minute, the run must still be applying after
# the last one, and its batches must have opened two files each.
#
function cd_tw_apply # fs...
{
	typeset -a fss=("$@")
	typeset -i nfs=${#fss[@]} i
	typeset fs
	typeset -i opened=$(clonedup_kstat dst_mounted)
	typeset -i batches=$(clonedup_kstat batches)

	log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 1000
	log_must zpool clonedup $TESTPOOL
	clonedup_wait_applying
	for ((i = 0; i < nsets; i++)); do
		fs=${fss[i % nfs]}
		clonedup_bounded 60 zfs set \
		    defaultuserquota=$((i / nfs % 2 + 1))G $fs
	done
	zpool status $TESTPOOL | grep -q "applying" ||
	    log_fail "the apply ended before the last set"

	((opened = $(clonedup_kstat dst_mounted) - opened))
	((batches = $(clonedup_kstat batches) - batches))
	log_note "$batches batches opened $opened files"
	log_must [ $batches -ge 2 ]
	log_must [ $opened -ge $((2 * batches)) ]

	log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 0
	log_must zpool wait -t clonedup $TESTPOOL
	clonedup_jstat_is errors 0
	clonedup_jstat_is clonedup_applied $((2 * nblk))
	clonedup_sync
}

log_assert "a teardown writer finishes while a batch holds two files"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
log_must save_tunable CLONEDUP_APPLY_THREADS
log_must save_tunable TXG_TIMEOUT

typeset all="$(clonedup_all_blocks $nblk)"

clonedup_pool_create
log_must set_tunable32 CLONEDUP_APPLY_THREADS 1
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 2
log_must set_tunable32 TXG_TIMEOUT 1

log_must zfs create $TESTPOOL/fs
clonedup_write /$TESTPOOL/fs/a $nblk
clonedup_dup /$TESTPOOL/fs/a /$TESTPOOL/fs/b
clonedup_dup /$TESTPOOL/fs/a /$TESTPOOL/fs/c
clonedup_sync
cd_tw_apply $TESTPOOL/fs
clonedup_check_shared $TESTPOOL/fs a $TESTPOOL/fs b "$all"
clonedup_check_shared $TESTPOOL/fs a $TESTPOOL/fs c "$all"
clonedup_leakcheck

#
# A block whose BRT range holds an entry outranks an older block
# as the source.  The first run left such entries on some vdevs,
# so the second run gets a new pool.  At two blocks a txg, a take
# from the index holds two copies of a block, in DVA order.  When
# p and q sorted first, p became the source of q and of s, and a
# batch held one file.  So the test writes s and finishes a run
# before it makes p and q.  The next run indexes only p and q,
# and its match walk finds s as their source.
#
destroy_pool $TESTPOOL
clonedup_pool_create
log_must zfs create $TESTPOOL/src
log_must zfs create $TESTPOOL/fs2
clonedup_write /$TESTPOOL/src/s $nblk
clonedup_sync
clonedup_run
clonedup_dup /$TESTPOOL/src/s /$TESTPOOL/fs2/p
clonedup_dup /$TESTPOOL/src/s /$TESTPOOL/fs2/q
clonedup_sync
cd_tw_apply $TESTPOOL/src $TESTPOOL/fs2
clonedup_check_shared $TESTPOOL/src s $TESTPOOL/fs2 p "$all"
clonedup_check_shared $TESTPOOL/src s $TESTPOOL/fs2 q "$all"
clonedup_leakcheck

log_pass "a teardown writer finishes while a batch holds two files"
