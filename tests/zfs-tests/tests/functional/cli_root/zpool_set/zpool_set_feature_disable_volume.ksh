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
#	A volume with blocks larger than 128K needs large_blocks from its
#	creation on, but activates it only when it writes its first block.
#	A creation that started before a disable of large_blocks, and whose
#	sync task runs after the disable's, must hold the feature until the
#	volume exists: the disable must then fail.
#
# STRATEGY:
#	1. Disable large_microzap, which depends on large_blocks.  Let
#	   transaction groups sync only when something waits for them,
#	   delay the pool's writes, and dirty 2M in the open transaction
#	   group, so that its sync writes for seconds before it runs its
#	   sync tasks.
#	2. Disable large_blocks in the background: its sync task is queued
#	   in that transaction group, and waiting for it starts the sync.
#	3. Once the sync has started, large_blocks still reads as enabled:
#	   create a volume with 1M blocks.  Its sync task runs in the next
#	   transaction group, after the disable's.
#	4. The disable must fail and leave large_blocks enabled, and fail
#	   again while the volume exists.  Once the volume is destroyed, it
#	   must succeed.
#

verify_runnable "global"

VDEV=$TEST_BASE_DIR/feature_volume.vdev
OUT=$TEST_BASE_DIR/feature_volume.out

function cleanup
{
	[[ -n "$disabler" ]] && wait $disabler
	zinject -c all >/dev/null 2>&1
	destroy_pool $TESTPOOL1
	rm -f $VDEV $OUT
	log_must restore_tunable TXG_TIMEOUT
	log_must restore_tunable TXG_HISTORY
}

function check_prop # feature value
{
	typeset val=$(get_pool_prop feature@$1 $TESTPOOL1)

	[[ "$val" == "$2" ]] ||
	    log_fail "feature@$1 is '$val', expected '$2'"
}

# The state of a transaction group in the pool's txg history: O while it is
# open, S while it syncs.
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

log_assert "A volume with 1M blocks created while large_blocks is being" \
    "disabled holds the feature"
log_onexit cleanup

log_must save_tunable TXG_TIMEOUT
log_must set_tunable32 TXG_TIMEOUT 3600
# Some tests turn the txg history off.
log_must save_tunable TXG_HISTORY
log_must set_tunable32 TXG_HISTORY 100

log_must truncate -s $MINVDEVSIZE $VDEV
log_must zpool create -f $TESTPOOL1 $VDEV
log_must zfs create $TESTPOOL1/fs
log_must zpool set feature@large_microzap=disabled $TESTPOOL1
check_prop large_blocks enabled
mntpnt=$(get_prop mountpoint $TESTPOOL1/fs)
sync_pool $TESTPOOL1

# 1. A transaction group that takes seconds to write its data.
log_must zinject -d $VDEV -D 200:1 $TESTPOOL1
log_must dd if=/dev/urandom of=$mntpnt/file bs=128k count=16
txg=$(open_txg)
[[ -n "$txg" ]] || log_fail "no open txg in: $(kstat_pool $TESTPOOL1 txgs)"

# 2. The disable's sync task is queued in that transaction group.
zpool set feature@large_blocks=disabled $TESTPOOL1 >$OUT 2>&1 &
disabler=$!
for i in $(seq 600); do
	[[ $(txg_state $txg) == S ]] && break
	sleep 0.1
done
[[ $(txg_state $txg) == S ]] || log_fail "txg $txg is not syncing:" \
    "$(kstat_pool $TESTPOOL1 txgs) $(cat $OUT)"

# 3. The creation starts before the disable's sync task runs.
check_prop large_blocks enabled
log_must zfs create -V 2m -b 1M $TESTPOOL1/vol

# 4.
wait $disabler
rc=$?
disabler=""
cat $OUT
(( rc != 0 )) || log_fail "large_blocks was disabled while a volume with" \
    "1M blocks was being created"
check_prop large_blocks enabled
log_must test "$(get_prop volblocksize $TESTPOOL1/vol)" = "1048576"
log_must zinject -c all
log_mustnot zpool set feature@large_blocks=disabled $TESTPOOL1
destroy_dataset $TESTPOOL1/vol
log_must zpool set feature@large_blocks=disabled $TESTPOOL1
check_prop large_blocks disabled

log_pass "A volume with 1M blocks created while large_blocks is being" \
    "disabled holds the feature"
