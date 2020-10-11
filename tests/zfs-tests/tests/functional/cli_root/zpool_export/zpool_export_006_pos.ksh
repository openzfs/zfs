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
# 1. Initiate a send from pool A to pool B.
# 2. Slow the send using pv, so it blocks a normal pool export.
# 3. Check that normal export of pool Bfails.
# 4. Forcibly export pool B.
# 5. Verify pool B is no longer present in the list output.
#

verify_runnable "global"

function cleanup {
	[[ -n "$sendpid" ]] && kill -9 "$sendpid"
	[[ -n "$recvpid" ]] && kill -9 "$recvpid"
	[[ -n "$pvpid" ]] && kill -9 "$pvpid"
	zpool_export_cleanup
}

log_onexit cleanup

log_assert "Verify a receiving pool can be forcibly exported."

srcsnap=$TESTPOOL1/$TESTFS@$TESTSNAP
dstsnap=$TESTPOOL2/$TESTFS@$TESTSNAP

vdev0=$TESTDIR0/$TESTFILE0
vdev1=$TESTDIR0/$TESTFILE1
log_must mkdir -p $TESTDIR0
log_must truncate -s 1G $vdev0 $vdev1
log_must zpool create -f $TESTPOOL1 $vdev0
log_must zpool create -f $TESTPOOL2 $vdev1
log_must zfs create $TESTPOOL1/$TESTFS

mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must dd if=/dev/urandom of=$mntpnt/$TESTFILE1 bs=1M count=16

log_must zfs snapshot $srcsnap

# Create FIFOs for send and receive, so the processes can be controlled and
# monitored individually.
create_fifo $TESTDIR0/sendfifo
create_fifo $TESTDIR0/recvfifo

zfs send $srcsnap > $TESTDIR0/sendfifo &
sendpid=$!
pv -L 1k < $TESTDIR0/sendfifo > $TESTDIR0/recvfifo &
pvpid=$!
zfs recv $dstsnap < $TESTDIR0/recvfifo &
recvpid=$!

log_note "zfs send pid is $sendpid, recv pid is $recvpid, pv pid is $pvpid"

log_note "Waiting until zfs receive has a chance to start ..."
typeset -i i=0
typeset -i timeout=5
while (( $i < $timeout )); do
	zfs list $TESTPOOL2/$TESTFS >/dev/null 2>&1 && break
	sleep 1
	((i = i + 1))
done
[[ $i -lt $timeout ]] || log_fail "receive failed to start"

log_must zfs list $TESTPOOL2/$TESTFS

log_mustnot zpool export $TESTPOOL2

# Send & receive should still be running; now try force export.
log_must kill -0 $sendpid
log_must kill -0 $recvpid
log_must zpool export -F $TESTPOOL2

# Both zfs send & recv should have exited non-zero.
log_mustnot wait $recvpid
log_mustnot wait $sendpid

poolexists $TESTPOOL1 || \
	log_fail "$TESTPOOL1 should be in 'zpool list' output."
poolexists $TESTPOOL2 && \
        log_fail "$TESTPOOL2 unexpectedly found in 'zpool list' output."

log_pass "Successfully forcibly exported a pool while receiving."
