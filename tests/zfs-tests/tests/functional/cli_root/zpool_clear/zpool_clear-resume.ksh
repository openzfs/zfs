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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
# Copyright (c) 2017 Lawrence Livermore National Security, LLC.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_clear/zpool_clear.cfg

#
# DESCRIPTION:
#	Verify 'zpool clear' can safely resume a suspended pool.
#
# STRATEGY:
#	1. Create a non-redundant pool with a small amount of data.
#	2. Inject an IO fault in one of the pools vdevs.
#	3. Force the pool to suspend itself by starting a scrub.
#	4. Verify the pool immediately re-suspends until the vdev is repaired.
#	5. Verify the pool will resume only after repairing the vdev.
#	6. Verify the pool will not resume if it was modified while suspended.
#	7. Verify the pool can only be exported using 'zpool export -F'.
#	8. Verify the pool can be imported normally.
#

verify_runnable "global"

function cleanup
{
        if poolexists $TESTPOOL1; then
		log_must zinject -c all
		log_must zpool export -F $TESTPOOL1
		log_must zpool import -d $TESTDIR1 $TESTPOOL1
		log_must zpool destroy -f $TESTPOOL1
	fi

	rm -f $VDEV1 $VDEV2 $VDEV3
	rm -f $TESTDIR1/${TESTPOOL1}*
	rmdir $TESTDIR1
}


log_assert "Verify 'zpool clear' can safely resume a suspended pool"
log_onexit cleanup

VDEV1=$TESTDIR1/vdev.0
VDEV2=$TESTDIR1/vdev.1
VDEV3=$TESTDIR1/vdev.2

log_must mkdir -p $TESTDIR1
log_must rm -f $TESTDIR1/${TESTPOOL1}*
log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2 $VDEV3

# Start a scrub and make a random vdev return IO errors.
function do_suspend # pool vdev
{
	typeset pool=$1
	typeset vdev=$2

	log_must zinject -a -d $vdev -e nxio $pool
	log_must eval "zpool scrub $pool &"
	log_must wait_suspend $pool
}

# Wait for pool to transition to a suspended state or timeout.
function wait_suspend # pool [timeout]
{
	typeset pool=$1
	typeset timeout=${2:-10}
	typeset count=0

	while ! is_pool_suspended $pool; do
		if (( count == $timeout )); then
			log_note "$pool did not suspend after ${timeout}s"
			return 1
		fi

		((count = count + 1))
		sleep 1
	done

	log_note "$pool successfully suspended"
	return 0
}

# Wait for resilver to complete or timeout.
function wait_resilver # pool [timeout]
{
	typeset pool=$1
	typeset timeout=${2:-10}
	typeset count=0

	while ! is_pool_resilvered $pool; do
		if (( count == $timeout )); then
			log_note "$pool failed to resilver after ${timeout}s"
			return 1
		fi

		((count = count + 1))
		sleep 1
	done

	log_note "$pool successfully resilvered"
	return 0
}

typeset file="/$TESTPOOL1/$TESTFS1/file"

log_must zpool create -f $TESTPOOL1 $VDEV1 $VDEV2 $VDEV3
log_must zfs create $TESTPOOL1/$TESTFS1

# Partially fill up the zfs filesystem to give scrub something to read.
avail=$(get_prop available $TESTPOOL1/$TESTFS1)
fill_mb=$(((avail / 1024 / 1024) * 5 / 100))
log_must dd if=/dev/urandom of=$file bs=$BLOCKSZ count=$fill_mb
sync_pool $TESTPOOL1

do_suspend $TESTPOOL1 $TESTDIR1/vdev.$(($RANDOM % 3))

log_note "Verify the pool immediately re-suspends until the vdev is repaired."
log_must zpool clear $TESTPOOL1
log_must wait_suspend $TESTPOOL1

log_note "Verify the pool will resume only after repairing the vdev."
log_must zinject -c all
log_must zpool clear $TESTPOOL1
log_mustnot wait_suspend $TESTPOOL1 3
if is_pool_suspended $TESTPOOL1; then
	log_fail "'zpool clear' failed to resume repaired pool"
fi

log_must wait_resilver $TESTPOOL1
log_must check_state $TESTPOOL1 "" ONLINE
log_must check_state $TESTPOOL1 $TESTDIR1/vdev.$i ONLINE

log_note "Verify the pool will not resume if it was modified while suspended."
do_suspend $TESTPOOL1 $TESTDIR1/vdev.$(($RANDOM % 3))
log_must zinject -c all
ztest -E -f $TESTDIR1 -p $TESTPOOL1 -T1 2>&1 >/dev/null
log_mustnot zpool clear $TESTPOOL1

log_note "Verify the pool can only be exported using 'zpool export -F'."
log_mustnot zpool export $TESTPOOL1
log_mustnot zpool export -f $TESTPOOL1
log_must zpool export -F $TESTPOOL1

log_note "Verify the pool can be imported normally."
log_must zpool import -d $TESTDIR1 $TESTPOOL1
log_must zpool destroy $TESTPOOL1

log_pass "'zpool clear' safely resumed a suspended pool."
