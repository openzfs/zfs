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
# Copyright (c) 2026 ConnectWise.
#

#
# DESCRIPTION:
# Destroying a clone with a populated livelist must not panic when the
# pool is space-constrained.
#
# If the pool is too full, deleting a clone's livelist in `sublist_delete_sync`
# or `livelist_delete_sync` may fail with ENOSPC.  Such errors are most likely
# transient; deferred frees will drain in future TXGs.  ZFS should handle these
# errors by waiting for the next TXG and retrying.
#
# STRATEGY:
# 1. Create a small pool so slop is spa_min_slop (128M).
# 2. Snapshot a filesystem and clone it; write many unique files (with a
#    small livelist sublist size) so deletion takes many txgs.
# 3. On a sibling filesystem, write a file larger than 3/4 of slop.
# 4. Destroy the clone while DESTROY-check space remains, queuing the
#    livelist for asynchronous deletion.  Must destroy the clone before setting
#    the reservation or `zfs destroy` will fail in userland without ever
#    submitting to the kernel.
# 5. Set a reservation on the sibling that consumes remaining NORMAL-check
#    space, then remove the large file so deferred frees accumulate while
#    root dd_used_bytes stays high.
# 6. Sync several txgs (the panic window), drop the reservation, and
#    wait for livelist deletion to finish.
#
# To verify that the intended code path was reached, while executing the test
# watch for the 'livelist...ENOSPC' message like this:
#
# dtrace -i 'sdt:openzfs::zfs-dbgmsg {printf("%s", stringof(args[0]));}' | egrep ENOSPC
# dtrace: description 'sdt:openzfs::zfs-dbgmsg ' matched 1 probe
#   1  88319                  none:zfs-dbgmsg spa.c:3400:spa_livelist_delete_enospc_backoff(): livelist 170 deletion hit ENOSPC, will retry
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

function cleanup
{
	log_must set_tunable64 LIVELIST_MAX_ENTRIES $ORIGINAL_MAX
	log_must set_tunable32 LIVELIST_MIN_PERCENT_SHARED $ORIGINAL_MIN
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	[[ -f $VIRTUAL_DISK ]] && log_must rm -f $VIRTUAL_DISK
}

ORIGINAL_MAX=$(get_tunable LIVELIST_MAX_ENTRIES)
ORIGINAL_MIN=$(get_tunable LIVELIST_MIN_PERCENT_SHARED)

log_onexit cleanup

VIRTUAL_DISK=$TEST_BASE_DIR/ll_enspc_disk
# 2G pool: slop is spa_min_slop (128M); DESTROY extra slop is 96M.
log_must truncate -s $(($MINVDEVSIZE * 8)) $VIRTUAL_DISK
log_must zpool create -f $TESTPOOL2 $VIRTUAL_DISK
log_must zfs set compression=off $TESTPOOL2

# Keep the livelist even if the clone diverges from the origin snapshot.
log_must set_tunable32 LIVELIST_MIN_PERCENT_SHARED -1
# One sublist per 1M file (8 x 128K records) so destroy walks
# sublist_delete_sync many times after zfs destroy returns.
log_must set_tunable64 LIVELIST_MAX_ENTRIES 8

log_must zfs create $TESTPOOL2/$TESTFS
log_must mkfile 5m /$TESTPOOL2/$TESTFS/atestfile
log_must zfs snapshot $TESTPOOL2/$TESTFS@snap
log_must zfs clone $TESTPOOL2/$TESTFS@snap $TESTPOOL2/$TESTCLONE

typeset i
for i in {1..40}; do
	log_must mkfile 1m /$TESTPOOL2/$TESTCLONE/testfile$i
	sync_pool $TESTPOOL2
done

zdb -vvvvv $TESTPOOL2/$TESTCLONE | grep Livelist >/dev/null || \
    log_fail "clone $TESTPOOL2/$TESTCLONE has no livelist"

# Sibling holds the reservation after destroy so the clone's livelist zthr
# is the thread that hits ENOSPC, not zfs destroy itself.
log_must zfs create $TESTPOOL2/rsrv
# Larger than DESTROY's extra slop (3/4 of 128M) so deferred frees can
# push used above the DESTROY quota once the reservation is in place.
log_must mkfile 256m /$TESTPOOL2/rsrv/bigfile
sync_pool $TESTPOOL2

log_must zfs destroy $TESTPOOL2/$TESTCLONE

typeset used=$(get_prop used $TESTPOOL2/rsrv)
typeset avail=$(get_prop available $TESTPOOL2/rsrv)
typeset resv=$((used + avail))
if ! zfs set reservation=$resv $TESTPOOL2/rsrv; then
	((resv -= 16 * 1024 * 1024))
	log_must zfs set reservation=$resv $TESTPOOL2/rsrv
fi
log_note "reservation on $TESTPOOL2/rsrv is $(get_prop reservation $TESTPOOL2/rsrv)"

# Free the file without waiting for deferred space to become allocatable.
log_must rm /$TESTPOOL2/rsrv/bigfile

#
# The livelist-delete zthr runs sublist_delete_sync in later txgs.  Sync
# several times so that thread hits the ENOSPC space check while mc_deferred
# is still elevated.  A panic here is the bug this test reproduces.
#
for i in {1..12}; do
	sync_pool $TESTPOOL2
done

log_must poolexists $TESTPOOL2

# Allow the livelist delete to finish once deferred space has drained.
log_must zfs set reservation=none $TESTPOOL2/rsrv
log_must zpool wait -t free $TESTPOOL2
sync_pool $TESTPOOL2

zdb -vvvvv $TESTPOOL2 | grep Livelist >/dev/null && \
    log_fail "livelist still present after clone destroy"

log_pass "Destroying a clone livelist under ENOSPC did not panic."
