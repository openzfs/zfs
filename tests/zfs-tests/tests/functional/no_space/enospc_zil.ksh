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
# Synchronous writes to a pool without a log device must not fill it with
# log blocks and suspend it.
#
# STRATEGY:
# 1. Create a pool on a 1 GiB file vdev: no log device, and too few
#    metaslabs for an embedded log, so log blocks come from the normal
#    class.
# 2. Fill it to leave about 200 MiB available.  Leaving less would let
#    the space estimate of the rewritten files end each txg on its own.
# 3. Raise the txg timeout and zfs_wrlog_data_max, so that only the
#    space check ends a txg, then rewrite four 512 KiB files with
#    sync=always for 120 seconds.
# 4. Fail if a log block allocation failed, which makes zil_commit()
#    fall back to a txg sync, or if the pool suspended.  A suspended
#    pool cannot be destroyed, so it is left in place under its own name.
#

verify_runnable "global"

typeset POOL=${TESTPOOL}zil
typeset VDEV=$TEST_BASE_DIR/enospc_zil_vdev
typeset -i suspended=0
typeset pids=""

function pool_suspended
{
	[[ $(kstat_pool $POOL state 2>/dev/null) == "SUSPENDED" ]]
}

function cleanup
{
	[[ -n "$pids" ]] && kill $pids 2>/dev/null
	wait
	restore_tunable TXG_TIMEOUT
	restore_tunable WRLOG_DATA_MAX
	if poolexists $POOL && pool_suspended; then
		log_note "$POOL is suspended, leaving it in place"
		return
	fi
	poolexists $POOL && destroy_pool $POOL
	rm -f $VDEV
}

log_onexit cleanup

log_assert "Synchronous writes do not fill a pool with log blocks"

log_must truncate -s 1G $VDEV
log_must zpool create -f -o failmode=continue $POOL $VDEV
log_must zfs create -o compression=off $POOL/fill
log_must zfs create -o compression=off -o recordsize=16k -o sync=always \
    $POOL/sync

typeset -i mib=$((1024 * 1024))
typeset -i fill=$((($(get_prop available $POOL) - 200 * mib) / mib))
log_must dd if=/dev/zero of=/$POOL/fill/file bs=1M count=$fill
for i in 1 2 3 4; do
	log_must dd if=/dev/urandom of=/$POOL/sync/file$i bs=16k count=32
done
sync_pool $POOL

log_must save_tunable TXG_TIMEOUT
log_must set_tunable32 TXG_TIMEOUT 120
log_must save_tunable WRLOG_DATA_MAX
log_must set_tunable64 WRLOG_DATA_MAX $((4096 * mib))

typeset -li free=$(get_pool_prop free $POOL)
typeset -li zil_before=$(kstat zil.zil_itx_metaslab_normal_alloc)
typeset -i errors_before=$(kstat zil.zil_commit_error_count)

function writer # file seconds
{
	typeset -i end=$((SECONDS + $2))

	while ((SECONDS < end)); do
		dd if=/dev/urandom of=$1 bs=16k count=32 conv=notrunc \
		    2>/dev/null || return 1
	done
}

for i in 1 2 3 4; do
	writer /$POOL/sync/file$i 120 &
	pids="$pids $!"
done

typeset -i deadline=$((SECONDS + 150))
while ((SECONDS < deadline)) && kill -0 $pids 2>/dev/null; do
	if pool_suspended; then
		suspended=1
		break
	fi
	sleep 1
done
pool_suspended && suspended=1

typeset -li zil=$(($(kstat zil.zil_itx_metaslab_normal_alloc) - zil_before))
typeset -i errors=$(($(kstat zil.zil_commit_error_count) - errors_before))
log_note "log written: $((zil / mib)) MiB, free at start:" \
    "$((free / mib)) MiB, commit errors: $errors"

((suspended)) && log_fail "The pool suspended"
((errors == 0)) || log_fail "$errors log block writes failed"
((zil > free)) || log_note "The log wrote less than the free space"

log_must wait $pids
pids=""
log_must zpool sync $POOL

log_pass "Synchronous writes do not fill a pool with log blocks"
