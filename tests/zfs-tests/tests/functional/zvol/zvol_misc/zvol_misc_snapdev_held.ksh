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
. $STF_SUITE/include/blkdev.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
# A volume's minor can be created again while its dataset stays held, so
# its objset stays cached between the two minors.
#
# STRATEGY:
# 1. Create a volume with some data, and a snapshot of it.
# 2. Send the snapshot into a pipe that is not read yet, so the send holds
#    the snapshot.
# 3. Make the snapshot's minor visible and hidden several times.
# 4. Verify that every change worked, then finish the send and verify that
#    the stream receives.
#

verify_runnable "global"

typeset vol=$TESTPOOL/heldvol
typeset snap=$vol@snap
typeset recv=$TESTPOOL/heldrecv
typeset go=$TEST_BASE_DIR/snapdev_held.go
typeset stream=$TEST_BASE_DIR/snapdev_held.zstream
typeset send_pid=""

function cleanup
{
	log_must touch $go
	[[ -n "$send_pid" ]] && wait $send_pid
	datasetexists $recv && destroy_dataset $recv -r
	datasetexists $vol && destroy_dataset $vol -r
	rm -f $go $stream
	block_device_wait
	is_linux && udev_cleanup
}

log_onexit cleanup

log_assert "A held snapshot's minor can be created more than once"

rm -f $go $stream
log_must zfs create -V 64M -o snapdev=hidden $vol
block_device_wait $ZVOL_DEVDIR/$vol
log_must dd if=/dev/urandom of=$ZVOL_DEVDIR/$vol bs=1M count=8 conv=fsync
log_must zfs snapshot $snap

# The stream is far larger than a pipe holds, so the send blocks with the
# snapshot held until the reader starts.
( zfs send $snap | ( while [[ ! -f $go ]]; do sleep 1; done;
    cat > $stream ) ) &
send_pid=$!
log_must sleep 2

for i in 1 2 3; do
	log_must zfs set snapdev=visible $vol
	block_device_wait $ZVOL_DEVDIR/$snap
	blockdev_exists $ZVOL_DEVDIR/$snap
	log_must zfs set snapdev=hidden $vol
	block_device_wait
	blockdev_missing $ZVOL_DEVDIR/$snap
done

log_must touch $go
log_must wait $send_pid
send_pid=""
log_must eval "zfs receive $recv < $stream"
log_must datasetexists $recv@snap

log_pass "A held snapshot's minor can be created more than once"
