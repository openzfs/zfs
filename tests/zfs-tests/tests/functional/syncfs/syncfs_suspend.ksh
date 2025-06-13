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

. $STF_SUITE/include/libtest.shlib

log_assert "syncfs() does not return success while the pool is suspended"

typeset -i syncfs_pid=0

function cleanup
{
	zinject -c all || true
	zpool clear $TESTPOOL || true
	test $syncfs_pid -gt 0 && kill -9 $syncfs_pid || true
	destroy_pool $TESTPOOL
}

log_onexit cleanup

DISK=${DISKS%% *}

# create a single-disk pool, set failmode=wait
log_must zpool create -o failmode=wait -f $TESTPOOL $DISK
log_must zfs create -o recordsize=128k $TESTPOOL/$TESTFS

# generate a write, then syncfs(), confirm success
log_must touch /$TESTPOOL/$TESTFS/file1
log_must sync -f /$TESTPOOL/$TESTFS

# and again with no changes
log_must sync -f /$TESTPOOL/$TESTFS

# set up error injections to force the pool to suspend on next write
log_must zinject -d $DISK -e io -T write $TESTPOOL
log_must zinject -d $DISK -e nxio -T probe $TESTPOOL

# generate another write
log_must touch /$TESTPOOL/$TESTFS/file2

# wait for the pool to suspend
log_note "waiting for pool to suspend"
typeset -i tries=10
until [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]] ; do
	if ((tries-- == 0)); then
		log_fail "pool didn't suspend"
	fi
	sleep 1
done

# pool suspended. syncfs() in the background, as it may block
sync -f /$TESTPOOL/$TESTFS &
syncfs_pid=$!

# give it a moment to get stuck
sleep 1

# depending on kernel version and pool config, syncfs should have either
# returned an error, or be blocked in the kernel
typeset -i blocked
typeset -i rc
if kill -0 $syncfs_pid ; then
	blocked=1
	log_note "syncfs() is blocked in the kernel"
else
	blocked=0
	log_note "syncfs() returned while pool was suspended"

	# exited, capture its error code directly
	wait $syncfs_pid
	rc=$?
	syncfs_pid=0
fi

# bring the pool back online
log_must zinject -c all
log_must zpool clear $TESTPOOL

if [[ $syncfs_pid -gt 0 ]] ; then
	# it blocked, clean it up now
	wait $syncfs_pid
	rc=$?
	syncfs_pid=0
fi

# if it returned when the pool was suspended, it must not claim success. if
# it blocked and returned after the pool suspended, then we don't care about
# the error (it depends on what happened after the pool resumed, which we're
# not testing here)
log_must test $blocked -eq 1 -o $rc -ne 0

log_pass "syncfs() does not return success while the pool is suspended"
