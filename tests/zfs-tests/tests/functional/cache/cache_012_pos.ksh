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
# Copyright (c) 2020, George Amanakis. All rights reserved.
#

. $STF_SUITE/tests/functional/cache/cache.cfg
. $STF_SUITE/tests/functional/cache/cache.kshlib

#
# DESCRIPTION:
#	Looping around a cache device with l2arc_write_size exceeding
#	the device size succeeds.
#
# STRATEGY:
#	1. Create pool with a cache device.
#	2. Set l2arc_write_max to a value larger than the cache device.
#	3. Create a file larger than the cache device and random read
#		for 10 sec.
#	4. Verify that l2arc_write_max is set back to the default.
#	5. Set l2arc_write_max to a value less than the cache device size but
#		larger than the default (64MB).
#	6. Record the l2_size.
#	7. Random read for 1 sec.
#	8. Record the l2_size again.
#	9. If (6) <= (8) then we have not looped around yet.
#	10. If (6) > (8) then we looped around. Break out of the loop and test.
#	11. Destroy pool.
#

verify_runnable "global"

command -v fio > /dev/null || log_unsupported "fio missing"

log_assert "Looping around a cache device succeeds."

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	log_must set_tunable32 L2ARC_WRITE_MAX $write_max
	log_must set_tunable32 L2ARC_NOPREFETCH $noprefetch
}
log_onexit cleanup

typeset write_max=$(get_tunable L2ARC_WRITE_MAX)
typeset noprefetch=$(get_tunable L2ARC_NOPREFETCH)
log_must set_tunable32 L2ARC_NOPREFETCH 0

typeset VDEV="$VDIR/vdev.disk"
typeset VDEV_SZ=$(( 4 * 1024 * 1024 * 1024 ))
typeset VCACHE="$VDIR/vdev.cache"
typeset VCACHE_SZ=$(( $VDEV_SZ / 2 ))

typeset fill_mb=$(( floor($VDEV_SZ * 3 / 4 ) ))
export DIRECTORY=/$TESTPOOL
export NUMJOBS=4
export RUNTIME=10
export PERF_RANDSEED=1234
export PERF_COMPPERCENT=66
export PERF_COMPCHUNK=0
export BLOCKSIZE=128K
export SYNC_TYPE=0
export DIRECT=1
export FILE_SIZE=$(( floor($fill_mb / $NUMJOBS) ))

log_must set_tunable32 L2ARC_WRITE_MAX $(( $VCACHE_SZ * 2 ))

log_must truncate -s $VCACHE_SZ $VCACHE
log_must truncate -s $VDEV_SZ $VDEV

log_must zpool create -f $TESTPOOL $VDEV cache $VCACHE

log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/random_reads.fio

typeset write_max2=$(get_tunable L2ARC_WRITE_MAX)

log_must test $write_max2 -eq $write_max

log_must set_tunable32 L2ARC_WRITE_MAX $(( 64 * 1024 * 1024 ))
export RUNTIME=1

typeset do_once=true
while $do_once || [[ $l2_size1 -le $l2_size2 ]]; do
	typeset l2_size1=$(get_arcstat l2_size)
	log_must fio $FIO_SCRIPTS/random_reads.fio
	typeset l2_size2=$(get_arcstat l2_size)
	do_once=false
done

log_must test $l2_size1 -gt $l2_size2

log_must zpool destroy $TESTPOOL

log_pass "Looping around a cache device succeeds."
