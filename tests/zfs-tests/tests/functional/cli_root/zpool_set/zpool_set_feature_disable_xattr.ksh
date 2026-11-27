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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/kstat.shlib

#
# DESCRIPTION:
#	A TX_SETSAXATTR record is logged if zilsaxattr is enabled, and the
#	commit that writes it activates the feature on the dataset first.
#	If the feature was disabled in between, the commit must not write
#	the record, but wait for its transaction group to sync instead, and
#	the disable must succeed.
#
# STRATEGY:
#	1. On a pool where zilsaxattr is disabled, give a file system with
#	   xattr=sa an intent log, so that a later commit does not create
#	   one, which would wait for a transaction group anyway.  Enable
#	   zilsaxattr: it is not active on the file system.
#	2. Let transaction groups sync only when something waits for them,
#	   delay the pool's writes, and dirty 2M in the open transaction
#	   group, so that its sync writes for seconds before it runs its
#	   sync tasks.
#	3. Disable zilsaxattr in the background: its sync task is queued in
#	   that transaction group, and waiting for it starts the sync.
#	4. Once the sync has started, zilsaxattr still reads as enabled:
#	   set an extended attribute, which logs a TX_SETSAXATTR record in
#	   the next transaction group, and dirty 2M more there.
#	5. Once zilsaxattr reads as disabled, fsync the file: it must
#	   return only once the record's transaction group has synced, and
#	   write no record.
#	6. The disable must succeed, and the extended attribute must be
#	   intact after the pool is imported again.
#

verify_runnable "global"

VDIR=$TEST_BASE_DIR/feature_xattr
VDEV=$VDIR/vdev
OUT=$TEST_BASE_DIR/feature_xattr.out

function cleanup
{
	[[ -n "$disabler" ]] && wait $disabler
	zinject -c all >/dev/null 2>&1
	destroy_pool $TESTPOOL1
	rm -rf $VDIR $OUT
	log_must restore_tunable TXG_TIMEOUT
	log_must restore_tunable TXG_HISTORY
	log_must restore_tunable ZIL_SAXATTR
}

function check_prop # feature value
{
	typeset val=$(get_pool_prop feature@$1 $TESTPOOL1)

	[[ "$val" == "$2" ]] ||
	    log_fail "feature@$1 is '$val', expected '$2'"
}

# The state of a transaction group in the pool's txg history: O while it is
# open, S while it syncs, C once it has synced.
function txg_state # txg
{
	kstat_pool $TESTPOOL1 txgs | awk -v txg=$1 '$1 == txg { s = $3 }
	    END { print s }'
}

function open_txg
{
	kstat_pool $TESTPOOL1 txgs | awk '$1 ~ /^[0-9]+$/ && $3 == "O" {
	    t = $1 } END { print t }'
}

log_assert "A TX_SETSAXATTR record whose commit comes after zilsaxattr" \
    "was disabled is not written, and the commit waits for its txg"
log_onexit cleanup

log_must save_tunable TXG_TIMEOUT
log_must set_tunable32 TXG_TIMEOUT 3600
# Some tests turn the txg history off.
log_must save_tunable TXG_HISTORY
log_must set_tunable32 TXG_HISTORY 100
log_must save_tunable ZIL_SAXATTR
log_must set_tunable32 ZIL_SAXATTR 1

# 1.
log_must mkdir -p $VDIR
log_must truncate -s $MINVDEVSIZE $VDEV
log_must zpool create -f -o feature@zilsaxattr=disabled $TESTPOOL1 $VDEV
log_must zfs create -o xattr=sa $TESTPOOL1/fs
log_must zfs create $TESTPOOL1/data
mntpnt=$(get_prop mountpoint $TESTPOOL1/fs)
datadir=$(get_prop mountpoint $TESTPOOL1/data)
log_must dd if=/dev/urandom of=$mntpnt/file bs=4k count=1 oflag=sync
log_must zpool set feature@zilsaxattr=enabled $TESTPOOL1
sync_pool $TESTPOOL1
check_prop zilsaxattr enabled

# 2. A transaction group that takes seconds to write its data.
log_must zinject -d $VDEV -D 200:1 $TESTPOOL1
log_must dd if=/dev/urandom of=$datadir/file1 bs=128k count=16
txg=$(open_txg)
[[ -n "$txg" ]] || log_fail "no open txg in: $(kstat_pool $TESTPOOL1 txgs)"

# 3. The disable's sync task is queued in that transaction group.
zpool set feature@zilsaxattr=disabled $TESTPOOL1 >$OUT 2>&1 &
disabler=$!
for i in $(seq 600); do
	[[ $(txg_state $txg) == S ]] && break
	sleep 0.1
done
[[ $(txg_state $txg) == S ]] || log_fail "txg $txg is not syncing:" \
    "$(kstat_pool $TESTPOOL1 txgs) $(cat $OUT)"

# 4. The record is logged before the disable's sync task runs.
log_must set_xattr test value $mntpnt/file
check_prop zilsaxattr enabled
log_must dd if=/dev/urandom of=$datadir/file2 bs=128k count=16

# 5.
for i in $(seq 600); do
	[[ $(get_pool_prop feature@zilsaxattr $TESTPOOL1) == disabled ]] &&
	    break
	sleep 0.1
done
check_prop zilsaxattr disabled
[[ $(txg_state $((txg + 1))) != C ]] ||
    log_fail "txg $((txg + 1)) synced before the fsync"
itxs=$(kstat_dataset $TESTPOOL1/fs zil_itx_count)
log_must dd if=/dev/null of=$mntpnt/file conv=notrunc,fsync
[[ $(txg_state $((txg + 1))) == C ]] || log_fail "the fsync returned" \
    "before txg $((txg + 1)) synced: $(kstat_pool $TESTPOOL1 txgs)"
log_must test "$(kstat_dataset $TESTPOOL1/fs zil_itx_count)" = "$itxs"

# 6.
wait $disabler
rc=$?
disabler=""
cat $OUT
(( rc == 0 )) || log_fail "zilsaxattr could not be disabled"
check_prop zilsaxattr disabled
log_must zinject -c all
log_must test "$(get_xattr test $mntpnt/file)" = "value"
log_must zpool export $TESTPOOL1
log_must zpool import -d $VDIR $TESTPOOL1
check_prop zilsaxattr disabled
log_must test "$(get_xattr test $mntpnt/file)" = "value"

log_pass "A TX_SETSAXATTR record whose commit comes after zilsaxattr" \
    "was disabled is not written, and the commit waits for its txg"
