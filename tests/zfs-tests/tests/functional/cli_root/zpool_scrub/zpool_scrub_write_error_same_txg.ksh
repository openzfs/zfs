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
# A write that fails in the txg of a scrub's setup gives the device a DTL
# that ends after that txg.  The scrub must still run to completion, not be
# cancelled and replaced by a resilver.
#
# STRATEGY:
# 1. Create a mirror pool on two file vdevs.
# 2. Keep writing to a file so that every txg has dirty data.
# 3. Fail writes to one child while "zpool scrub" sets up the scrub.
# 4. Wait for the scans to finish.
# 5. Check that the scrub ran to completion.  A resilver of the writes
#    that failed after its setup may follow it.
#

verify_runnable "global"

function cleanup
{
	[[ -n "$writer_pid" ]] && kill $writer_pid 2>/dev/null
	wait
	zinject -c all >/dev/null 2>&1
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -f $VDEV1 $VDEV2
}

log_assert "A write error in the txg of a scrub's setup does not cancel" \
	"the scrub"
log_onexit cleanup

VDEV1=$TEST_BASE_DIR/scrub_write_error_vdev1
VDEV2=$TEST_BASE_DIR/scrub_write_error_vdev2

log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2
log_must zpool create -f $TESTPOOL1 mirror $VDEV1 $VDEV2
log_must zfs set compression=off $TESTPOOL1
MNT=$(get_prop mountpoint $TESTPOOL1)

while true; do
	dd if=/dev/urandom of=$MNT/file bs=1M count=16 conv=notrunc \
	    status=none
done &
writer_pid=$!
sleep 4

log_must zinject -d $VDEV2 -e io -T write $TESTPOOL1
log_must zpool scrub $TESTPOOL1
log_must zinject -c all
kill $writer_pid
wait $writer_pid
writer_pid=""

log_must zpool wait -t scrub,resilver $TESTPOOL1
log_must zpool status -v $TESTPOOL1
history=$(zpool history -i $TESTPOOL1 | grep -E "scan (setup|aborted|done)")
log_note "$history"
next=$(echo "$history" | awk '/scan setup func=1 /{ found = 1; next }
	found { print; exit }')
[[ -n "$next" ]] || log_fail "scrub setup or its end not found in history"
[[ "$next" == *"scan done"* ]] || log_fail "the scrub did not complete: $next"

log_pass "A write error in the txg of a scrub's setup does not cancel" \
	"the scrub"
