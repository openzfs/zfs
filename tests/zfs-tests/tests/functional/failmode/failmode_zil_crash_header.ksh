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
# Copyright (c) 2026, Will Rouesnel <wrouesnel@wrouesnel.com>
#

#
# DESCRIPTION:
#	After the ZIL crashes and frees its log blocks, the on-disk ZIL
#	header must not point at any of them.
#
# STRATEGY:
#	1. Create a pool with failmode=continue and a file system with no
#	   ZIL chain yet, then set sync=always.
#	2. Make writes and probes fail and write to the file. The write's
#	   zil_commit() creates the ZIL chain: zil_create() allocates the
#	   first log block and waits for its txg, which writes the header
#	   with that block, fails and suspends the pool. zil_create() gets
#	   ESHUTDOWN and crashes the ZIL.
#	3. Resume the pool and let a few txgs sync, so that the crashed ZIL
#	   frees its log block. Don't touch the file system.
#	4. Check the pool with zdb -b: no log block may be referenced by the
#	   on-disk header and free in the space maps.
#	5. Destroy the file system, which frees the log chain the header
#	   points at, and check the pool again.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/failmode/failmode.kshlib

verify_runnable "global"

typeset desc="the on-disk ZIL header outlives no log block after a ZIL crash"

log_assert $desc
log_onexit failmode_sync_cleanup

read -r DISK1 _ <<<"$DISKS"

typeset datafile="/$TESTPOOL/$TESTFS/datafile"

log_must zpool create -f -o failmode=continue $TESTPOOL $DISK1
log_must zfs create $TESTPOOL/$TESTFS

# write the file before sync=always, so that it doesn't start a ZIL chain
log_must dd if=/dev/urandom of=$datafile bs=128k count=1
log_must zfs set sync=always $TESTPOOL/$TESTFS
sync_pool $TESTPOOL

# the file system has no ZIL chain yet
log_mustnot eval "zdb -iv $TESTPOOL/$TESTFS | grep -q 'Block seqno'"

typeset -i crash_before=$(kstat zil.zil_commit_error_count)

zinject -d $DISK1 -e io -T write $TESTPOOL
zinject -d $DISK1 -e nxio -T probe $TESTPOOL

# with failmode=continue the write fails once the pool suspends
dd if=/dev/urandom of=$datafile bs=4k count=1 conv=notrunc &
typeset -i pid=$!

log_note "waiting for pool to suspend"
typeset -i tries=10
until [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]] ; do
	if ((tries-- == 0)); then
		log_fail "pool didn't suspend"
	fi
	sleep 1
done

log_must zinject -c all
log_must zpool clear $TESTPOOL
wait $pid
log_note "write returned $?"

# the ZIL must have seen the error
log_must test $(kstat zil.zil_commit_error_count) -gt $crash_before

# let the crashed ZIL free its blocks and restart
for i in {1..6}; do
	sync_pool $TESTPOOL
done

log_must zdb -b $TESTPOOL

log_must zfs destroy $TESTPOOL/$TESTFS
for i in {1..4}; do
	sync_pool $TESTPOOL
done
log_must zdb -b $TESTPOOL

log_pass $desc
