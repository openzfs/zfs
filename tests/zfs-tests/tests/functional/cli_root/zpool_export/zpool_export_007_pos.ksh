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
# A pool should be force exportable, while POSIX I/O is in flight.
#
# STRATEGY:
# 1. Write to a file that is held open, slowed using pv, so it blocks a
#    normal filesystem unmount / pool export.
# 2. Check that normal export fails.
# 3. Forcibly export pool.
# 4. Verify pool is no longer present in the list output.
#

verify_runnable "global"

function cleanup {
	[[ -n "$ddinpid" ]] && kill -9 "$ddinpid"
	[[ -n "$ddoutpid" ]] && kill -9 "$ddoutpid"
	if is_linux; then
		log_must set_tunable64 FORCED_EXPORT_UNMOUNT 0
	fi
	zpool_export_cleanup
}

log_onexit cleanup

log_assert "Verify a pool can be forcibly exported while writing POSIX I/O"

snap=$TESTPOOL1/$TESTFS@$TESTSNAP
snapstream=$TEST_BASE_DIR/send.$$

# On Linux, it's necessary to enable a tunable for the test to be able to
# kick the POSIX I/O user off.
if is_linux; then
	log_must set_tunable64 FORCED_EXPORT_UNMOUNT 1
fi

vdev0=$TESTDIR0/$TESTFILE0
log_must mkdir -p $TESTDIR0
log_must truncate -s 1G $vdev0
log_must zpool create -f $TESTPOOL1 $vdev0
log_must zfs create $TESTPOOL1/$TESTFS

mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)

# Create FIFOs for the writes, so the processes can be controlled and
# monitored individually.
create_fifo $TESTDIR0/writefifo
dd if=/dev/urandom bs=1M count=16 | pv -L 1k > $TESTDIR0/writefifo &
ddinpid=$!

dd of=${mntpnt}/$TESTFILE1 < $TESTDIR0/writefifo &
ddoutpid=$!

log_note "dd input pid is $ddinpid, dd output pid is $ddoutpid"

log_note "Waiting until output file is filling ..."
typeset -i i=0
typeset -i timeout=5
while (( $i < $timeout )); do
	test -f ${mntpnt}/$TESTFILE1 && break
	sleep 1
	((i = i + 1))
done
[[ $i -lt $timeout ]] || log_fail "dd failed to start"

log_mustnot zpool export $TESTPOOL1

# Write should still be running; now try force export.  We must do this
# twice so dd dies initially.
log_must kill -0 $ddoutpid
log_mustnot zpool export -F $TESTPOOL1
# Write should have exited non-zero.
log_mustnot wait $ddoutpid
log_must zpool export -F $TESTPOOL1

poolexists $TESTPOOL1 && \
        log_fail "$TESTPOOL1 unexpectedly found in 'zpool list' output."

log_pass "Successfully forcibly exported a pool while writing POSIX I/O sending."
