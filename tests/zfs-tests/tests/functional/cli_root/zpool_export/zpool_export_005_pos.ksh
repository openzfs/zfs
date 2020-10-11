#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2020 by Klara Systems, Inc.  All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zpool_export/zpool_export.kshlib

#
# DESCRIPTION:
# A pool should be force exportable, while a send is running from it.
#
# STRATEGY:
# 1. Initiate a send from pool to a file.
# 2. Slow the send using pv, so it blocks a normal pool export.
# 3. Check that normal export fails.
# 4. Forcibly export pool.
# 5. Verify pool is no longer present in the list output.
#

verify_runnable "global"

function cleanup {
	[[ -n "$sendpid" ]] && kill -9 "$sendpid"
	[[ -n "$pvpid" ]] && kill -9 $pvpid
	[[ -n "$snapstream" ]] && rm -f "$snapstream"
	zpool_export_cleanup
}

log_onexit cleanup

log_assert "Verify a pool can be forcibly exported while sending."

snap=$TESTPOOL1/$TESTFS@$TESTSNAP
snapstream=$TEST_BASE_DIR/send.$$

vdev0=$TESTDIR0/$TESTFILE0
log_must mkdir -p $TESTDIR0
log_must truncate -s 1G $vdev0
log_must zpool create -f $TESTPOOL1 $vdev0
log_must zfs create $TESTPOOL1/$TESTFS

mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must dd if=/dev/urandom of=$mntpnt/$TESTFILE1 bs=1M count=16

log_must zfs snapshot $snap

# Create FIFOs for the send, so the processes can be controlled and
# monitored individually.
create_fifo $TESTDIR0/snapfifo
zfs send $snap > $TESTDIR0/snapfifo &
sendpid=$!
pv -L 1k < $TESTDIR0/snapfifo > $snapstream &
pvpid=$!

log_note "zfs send pid is $sendpid, pv pid is $pvpid"

log_mustnot zpool export $TESTPOOL1

# Send should still be running; now try force export.
log_must kill -0 $sendpid
log_must zpool export -F $TESTPOOL1

lsout=$(ls -l $snapstream)
log_note "snapstream: $lsout"

# Send should have exited non-zero.
log_mustnot wait $sendpid

poolexists $TESTPOOL1 && \
        log_fail "$TESTPOOL1 unexpectedly found in 'zpool list' output."

log_pass "Successfully forcibly exported a pool while sending."
