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
# Copyright (c) 2026 by the OpenZFS project.  All rights reserved.
#
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A sync op that starts on a pool that is already suspended behaves as
#	one that was running when the pool suspended: syncfs(2) returns
#	without waiting (zfs_sync() must not block on a suspended pool),
#	fsync(2) returns EIO with failmode=continue, and with failmode=wait
#	it waits for the pool to resume and then succeeds.
#
#	On FreeBSD, zfs_sync() commits the ZIL as fsync(2) does, following
#	failmode=, and sync(8) ignores its arguments (it is always sync(2)):
#	sync(2) must return with failmode=continue, and is not run with
#	failmode=wait, where it waits for the pool to resume by design.
#
#	On Linux before 5.8, sync_filesystem() ignores the error sync_fs()
#	returns, and unless the kernel has super_block s_wb_err to carry it,
#	zpl_sync_fs() waits for the pool instead of reporting a sync that did
#	not happen (#17416): syncfs(2) is run there, but whether it returns
#	is not checked; it must finish once the pool resumes.
#
# STRATEGY:
#	For failmode=wait and failmode=continue:
#	1. Create a pool and a file, and suspend the pool with write errors
#	   during a txg sync, so that the ZIL has not crashed
#	2. Start syncfs(2) on the dataset (Linux) or sync(2) (FreeBSD,
#	   failmode=continue only): it must return within 10 seconds (not
#	   checked on Linux before 5.8)
#	3. Start fsync(2) on the file: with continue it must fail with EIO
#	   within 10 seconds; with wait it must still be waiting
#	4. Resume the pool: a waiting fsync must then succeed, and a waiting
#	   syncfs(2) must return
#	5. The pool must be ONLINE, the file intact, and a new fsync, export
#	   and import must work
#

verify_runnable "global"

typeset spid="" fpid="" ddpid=""
DISK=${DISKS%% *}

function cleanup
{
	zinject -c all
	poolexists $TESTPOOL && zpool clear $TESTPOOL
	for p in $spid $fpid $ddpid; do
		wait $p
	done
	spid="" fpid="" ddpid=""
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $TEST_BASE_DIR/sync_suspended.src
	rm -f $TEST_BASE_DIR/sync_suspended.err
}

# returns within secs?  wait_exit pid secs
function wait_exit
{
	typeset -i n=$2
	while ((n-- > 0)); do
		kill -0 $1 2>/dev/null || return 0
		sleep 1
	done
	kill -0 $1 2>/dev/null && return 1
	return 0
}

log_assert "sync ops started on an already suspended pool follow failmode="
log_onexit cleanup

typeset src=$TEST_BASE_DIR/sync_suspended.src
typeset err=$TEST_BASE_DIR/sync_suspended.err
log_must dd if=/dev/urandom of=$src bs=128k count=4

for failmode in wait continue; do
	log_note "failmode=$failmode"
	log_must zpool create -f -o failmode=$failmode $TESTPOOL $DISK
	typeset -i crash0=$(kstat zil.zil_commit_crash_count)
	log_must zfs create $TESTPOOL/$TESTFS
	typeset mnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
	log_must cp $src $mnt/file
	log_must sync_pool $TESTPOOL

	# Suspend the pool: writes and the probe that follows them fail.
	# The write is synced by a txg, not the ZIL, so that the ZIL is not
	# already crashed (a crashed ZIL takes another path).
	log_must zinject -d $DISK -e io -T write $TESTPOOL
	log_must zinject -d $DISK -e nxio -T probe $TESTPOOL
	log_must dd if=/dev/urandom of=$mnt/fill bs=128k count=8
	zpool sync $TESTPOOL &
	ddpid=$!
	typeset -i tries=30
	until [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]]; do
		((tries-- > 0)) || log_fail "pool did not suspend"
		sleep 1
	done

	if is_linux && [[ $(linux_version) -lt $(linux_version "5.8") ]]
	then
		# Before 5.8, zpl_sync_fs() may have no way to report the
		# error, and then waits for the pool by design (see the
		# comment there).  Don't check it; it must return after resume.
		sync -f $mnt &
		spid=$!
		if wait_exit $spid 10; then
			wait $spid
			log_note "syncfs returned $? while suspended"
			spid=""
		else
			log_note "syncfs is waiting for the pool (Linux < 5.8)"
		fi
	elif is_linux; then
		# syncfs(2) must not wait for the suspended pool, whatever
		# failmode=.
		sync -f $mnt &
		spid=$!
		wait_exit $spid 10 || log_fail \
		    "syncfs blocked on the suspended pool (failmode=$failmode)"
		wait $spid
		log_note "syncfs returned $? while suspended"
		spid=""
	elif [[ $failmode == "continue" ]]; then
		# FreeBSD: sync(8) ignores its arguments, and sync(2) reaches
		# zfs_sync(), which commits the ZIL following failmode=, so with
		# continue it must return.  With failmode=wait it waits for the
		# pool to resume by design, so it is not checked there.
		sync &
		spid=$!
		wait_exit $spid 10 || log_fail \
		    "sync blocked on the suspended pool (failmode=$failmode)"
		wait $spid
		log_note "sync returned $? while suspended"
		spid=""
	fi

	# fsync(2) of an existing file, no new data: dd writes nothing, but
	# opens the file without truncating it and fsyncs it on both OSes.
	dd if=/dev/null of=$mnt/file conv=notrunc,fsync 2>$err &
	fpid=$!
	if [[ $failmode == "continue" ]]; then
		wait_exit $fpid 10 || \
		    log_fail "fsync blocked with failmode=continue"
		wait $fpid
		typeset -i rc=$?
		fpid=""
		((rc != 0)) || log_fail "fsync succeeded on a suspended pool"
		grep -qE "Input/output error|I/O error" $err || \
		    log_fail "fsync did not fail with EIO: $(<$err)"
		log_note "fsync failed with EIO while suspended, as expected"
	else
		wait_exit $fpid 5 && \
		    log_fail "fsync returned on a suspended failmode=wait pool"
		log_note "fsync is waiting for the pool to resume, as expected"
	fi

	# The ops above must have gone through the ZIL, not a crashed one.
	typeset -i crash1=$(kstat zil.zil_commit_crash_count)
	((crash1 == crash0)) || log_fail "ZIL crashed before the sync ops"

	# Resume the pool.
	log_must zinject -c all
	log_must zpool clear $TESTPOOL
	if [[ -n "$fpid" ]]; then
		wait $fpid
		typeset -i rc=$?
		fpid=""
		((rc == 0)) || \
		    log_fail "fsync failed after resume (rc $rc): $(<$err)"
	fi
	if [[ -n "$spid" ]]; then
		wait $spid
		log_note "syncfs returned $? after resume"
		spid=""
	fi
	wait $ddpid
	ddpid=""

	log_must check_state $TESTPOOL "" "ONLINE"
	log_must cmp $src $mnt/file
	log_must dd if=$src of=$mnt/file2 bs=128k conv=fsync
	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL
	log_must cmp $src $mnt/file
	log_must cmp $src $mnt/file2
	log_must destroy_pool $TESTPOOL
done

log_pass "sync ops started on an already suspended pool follow failmode="
