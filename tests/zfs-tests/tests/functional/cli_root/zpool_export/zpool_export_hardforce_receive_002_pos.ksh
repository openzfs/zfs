#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/tests/functional/cli_root/zpool_export/zpool_export.kshlib

#
# DESCRIPTION:
# A suspended pool should be hardforce exportable, while a receive is running.
#
# STRATEGY:
# 1. Initiate a send from one pool and receive to another pool.
# 2. Slow the send using pv, so it blocks a normal pool export.
# 3. Suspend the pool.
# 4. Check that normal export fails.
# 5. Forcibly export pool.
# 6. Verify pool is no longer present in the list output.
#

verify_runnable "global"

function cleanup
{
	# The test may fail and leave a sleeping spa_namespace_lock holder.
	# Let's unbreak it first.
	zpool export -F $TESTPOOL

	clear_suspension_artifacts $TESTPOOL

	[[ -n "$sendpid" ]] && kill -9 "$sendpid"
	[[ -n "$recvpid" ]] && kill -9 "$recvpid"
	[[ -n "$pvpid" ]] && kill -9 $pvpid
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	[[ -n "$file0" ]] && rm -f "$file0"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}
log_onexit cleanup

log_assert "Verify a suspended pool can be forcibly exported while receiving."

mkdir -p $TESTDIR
dstsnap=$TESTPOOL/$TESTFS@$TESTSNAP
srcsnap=$TESTPOOL2/$TESTFS@$TESTSNAP
sendfifo=$TESTDIR/sendfifo.$$
recvfifo=$TESTDIR/recvfifo.$$
file0=$TESTDIR/file0.$$

log_must create_pool $TESTPOOL raidz $FEDISK0 $FEDISK1 $FEDISK2

log_must truncate -s 1G $file0
log_must create_pool $TESTPOOL2 $file0
log_must zfs create $TESTPOOL2/$TESTFS
log_must dd if=/dev/urandom of=/$TESTPOOL2/$TESTFS/$TESTFILE1 bs=1M count=128
log_must zfs snapshot $srcsnap

log_must mkfifo $sendfifo
log_must mkfifo $recvfifo
zfs send $srcsnap > $sendfifo &
sendpid=$!
pv -L 1M < $sendfifo > $recvfifo &
pvpid=$!
zfs recv $dstsnap < $recvfifo &
recvpid=$!

log_note "zfs send pid is $sendpid, pv pid is $pvpid, recv pid is $recvpid"

log_note "Waiting until zfs receive has a chance to start ..."
for i in {1..5}; do
	zfs list $TESTPOOL/$TESTFS && break
	sleep 1
done
log_must zfs list $TESTPOOL/$TESTFS

# Send & receive should still be running
log_must kill -0 $sendpid
log_must kill -0 $recvpid

log_must suspend_pool $TESTPOOL $FEDISK0 $FEDISK2

# Send should still be running; now try force export.
log_must kill -0 $sendpid
log_must kill -0 $recvpid
log_must zpool export -F $TESTPOOL

# Send should have exited with non-zero.
log_mustnot wait $sendpid
log_must wait $pvpid
log_mustnot wait $recvpid

poolexists $TESTPOOL && \
	log_fail "$TESTPOOL unexpectedly found in 'zpool list' output."

log_pass "Successfully forcibly exported a suspended pool while receiving."
