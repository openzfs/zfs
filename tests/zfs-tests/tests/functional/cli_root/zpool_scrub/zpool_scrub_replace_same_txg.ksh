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
# A scrub whose setup syncs in the same txg as a "zpool replace" must not
# cancel the resilver the replace scheduled, and must not report errors for
# healthy blocks.
#
# STRATEGY:
# 1. Create a pool on one file vdev.
# 2. Delay the write of one dirty block with "zinject -E", so that the
#    sync of txg A stalls while it holds the config lock as reader.
# 3. Suspend scan progress, so that the scrub cannot finish, and start a
#    scrub; its setup task goes into txg A+1.
# 4. Start "zpool replace"; it waits for txg A to finish syncing, so it
#    also lands in txg A+1, gives the new device a DTL that ends after that
#    txg, and schedules a resilver restart for the txg where the DTL ends.
#    It returns once that txg has synced.
# 5. Record whether that restart started a resilver (without the fix, the
#    scrub's setup cancels it).
# 6. Resume scan progress and wait for the scans and the replace to finish.
# 7. Check in the pool history that the scrub's setup saw the new DTL.
# 8. Check that the resilver started in step 5, that the new device was
#    resilvered, and that the pool has no data errors (without the fix, the
#    scrub can read blocks rewritten later in its setup txg).
#

verify_runnable "global"

function cleanup
{
	wait
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	zinject -c all >/dev/null 2>&1
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -f $VDEV1 $VDEV2
}

log_assert "A scrub set up in the txg of a replace keeps its resilver" \
	"and reports no false errors"
log_onexit cleanup

VDEV1=$TEST_BASE_DIR/scrub_replace_vdev1
VDEV2=$TEST_BASE_DIR/scrub_replace_vdev2
DELAY_MS=15000
TXG_CONCURRENT_STATES=3		# include/sys/txg.h

log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2
log_must zpool create -f $TESTPOOL1 $VDEV1
MNT=$(get_prop mountpoint $TESTPOOL1)
log_must touch $MNT/stall
sync_pool $TESTPOOL1

#
# Stall the sync of the next txg (A) on the write of "stall".  "zpool sync"
# returns once txg A+2 has synced, after the stall.
#
log_must zinject -E $DELAY_MS -T write -t data $MNT/stall
log_must dd if=/dev/urandom of=$MNT/stall bs=128k count=1
zpool sync $TESTPOOL1 &
sync_pid=$!
sleep 2

#
# The scrub's setup task goes into txg A+1.  The scrub must get past
# spa_open() before the replace takes spa_namespace_lock and holds it until
# its own txgs have synced.
#
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
zpool scrub $TESTPOOL1 &
scrub_pid=$!
sleep 3

# Waits for txg A to sync, then attaches VDEV2 in txg A+1.
zpool replace $TESTPOOL1 $VDEV1 $VDEV2 &
replace_pid=$!

log_must wait $replace_pid
wait $scrub_pid || log_fail "the scrub did not start before the replace"
log_must wait $sync_pid
log_must zinject -c all

#
# The scrub cannot finish, so only the restart the replace scheduled can have
# started a resilver.
#
restarted=false
zpool history -i $TESTPOOL1 | grep -q "scan setup func=2 " && restarted=true
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t replace,resilver,scrub $TESTPOOL1

#
# The replace gives VDEV2 a DTL that ends TXG_CONCURRENT_STATES after its
# txg.  That end must show up as the max txg of the resilver.
#
history=$(zpool history -i $TESTPOOL1 | grep "scan setup")
log_note "$history"
scrub_txg=$(echo "$history" | awk '/func=1 /{
	sub(/.*\[txg:/, ""); sub(/\].*/, ""); txg = $0 } END { print txg }')
[[ -n "$scrub_txg" ]] || log_fail "scrub setup not found in history"
want=$((scrub_txg + TXG_CONCURRENT_STATES))
echo "$history" | grep -q "maxtxg=$want\$" || \
    log_fail "scrub setup in txg $scrub_txg did not race with the replace"

$restarted || \
    log_fail "the scrub cancelled the resilver that the replace scheduled"
log_must zpool status -v $TESTPOOL1
log_must is_pool_resilvered $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

log_pass "A scrub set up in the txg of a replace keeps its resilver" \
	"and reports no false errors"
