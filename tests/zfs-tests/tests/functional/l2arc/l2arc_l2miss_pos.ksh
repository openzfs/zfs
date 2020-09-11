#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2020, Adam Moss. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/l2arc/l2arc.cfg

#
# DESCRIPTION:
#	l2arc_misses does not increment upon reads from a pool without l2arc
#
# STRATEGY:
#	1. Create pool with a cache device.
#	2. Create pool without a cache device.
#	3. Create a random file in the no-cache-device pool,
#		and random read for 10 sec.
#	4. Check that l2arc_misses hasn't risen
#	5. Create a random file in the pool with the cache device,
#		and random read for 10 sec.
#	6. Check that l2arc_misses has risen
#

verify_runnable "global"

log_assert "l2arc_misses does not increment upon reads from a pool without l2arc."

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	if poolexists $TESTPOOL1 ; then
		destroy_pool $TESTPOOL1
	fi
}
log_onexit cleanup

typeset fill_mb=800
typeset cache_sz=$(( 1.4 * $fill_mb ))
export FILE_SIZE=$(( floor($fill_mb / $NUMJOBS) ))M

log_must truncate -s ${cache_sz}M $VDEV_CACHE

log_must zpool create -O compression=off -f $TESTPOOL $VDEV cache $VDEV_CACHE
log_must zpool create -O compression=off -f $TESTPOOL1 $VDEV1

# I/O to pool without l2arc - expect that l2_misses stays constant
export DIRECTORY=/$TESTPOOL1
log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/random_reads.fio
# attempt to remove entries for pool from ARC so we would try
#    to hit the nonexistent L2ARC for subsequent reads
log_must zpool export $TESTPOOL1
log_must zpool import $TESTPOOL1 -d $VDEV1

typeset starting_miss_count=$(get_arcstat l2_misses)

log_must fio $FIO_SCRIPTS/random_reads.fio
log_must test $(get_arcstat l2_misses) -eq $starting_miss_count

# I/O to pool with l2arc - expect that l2_misses rises
export DIRECTORY=/$TESTPOOL
log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/random_reads.fio
# wait for L2ARC writes to actually happen
arcstat_quiescence_noecho l2_size
# attempt to remove entries for pool from ARC so we would try
#    to hit L2ARC for subsequent reads
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL -d $VDEV

log_must fio $FIO_SCRIPTS/random_reads.fio
log_must test $(get_arcstat l2_misses) -gt $starting_miss_count

log_must zpool destroy -f $TESTPOOL
log_must zpool destroy -f $TESTPOOL1

log_pass "l2arc_misses does not increment upon reads from a pool without l2arc."
