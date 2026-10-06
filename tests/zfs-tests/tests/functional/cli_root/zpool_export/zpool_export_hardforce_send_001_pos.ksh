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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/tests/functional/cli_root/zpool_export/zpool_export.kshlib

#
# DESCRIPTION:
# A pool should be hardforce exportable, while a send is running from it.
#
# STRATEGY:
# 1. Initiate a send from pool to a file.
# 2. Slow the send using pv, so it blocks a normal pool export.
# 3. Check that normal export fails.
# 4. Forcibly export pool.
# 5. Verify pool is no longer present in the list output.
#

verify_runnable "global"

function cleanup
{
	[[ -n "$sendpid" ]] && kill -9 "$sendpid"
	[[ -n "$pvpid" ]] && kill -9 $pvpid
	[[ -n "$snapstream" ]] && rm -f "$snapstream"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}
log_onexit cleanup

log_assert "Verify a pool can be forcibly exported while sending."

mkdir -p $TESTDIR
snap=$TESTPOOL/$TESTFS@$TESTSNAP
snapfifo=$TESTDIR/snapfifo.$$
snapstream=$TESTDIR/send.$$

log_must create_pool $TESTPOOL raidz $FEDISK0 $FEDISK1 $FEDISK2
log_must zfs create $TESTPOOL/$TESTFS
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/$TESTFILE1 bs=1M count=128
log_must zfs snapshot $snap

# Create FIFOs for the send, so the processes can be controlled and
# monitored individually.
log_must mkfifo $snapfifo
zfs send $snap > $snapfifo &
sendpid=$!
pv -L 1M < $snapfifo > $snapstream &
pvpid=$!

log_note "zfs send pid is $sendpid, pv pid is $pvpid"

# Send should still be running; non-forced export should yield.
log_must kill -0 $sendpid
log_must kill -0 $pvpid
log_mustnot zpool export $TESTPOOL

# Send should still be running; now try force export.
log_must kill -0 $sendpid
log_must kill -0 $pvpid
log_must zpool export -F $TESTPOOL

# Send should have exited with non-zero.
log_mustnot wait $sendpid
log_must wait $pvpid

poolexists $TESTPOOL && \
	log_fail "$TESTPOOL unexpectedly found in 'zpool list' output."

log_pass "Successfully forcibly exported a pool while sending."
