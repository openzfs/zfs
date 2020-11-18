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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/l2arc/l2arc.cfg

#
# DESCRIPTION:
#	Off/onlining an L2ARC device restores all written blocks, vdev present.
#
# STRATEGY:
#	1. Create pool with a cache device.
#	2. Create a random file in that pool and random read for 10 sec.
#	3. Read the amount of log blocks written from the header of the
#		L2ARC device.
#	4. Offline the L2ARC device.
#	5. Online the L2ARC device.
#	6. Read the amount of log blocks rebuilt in arcstats and compare to
#		(3).
#	7. Create another random file in that pool and random read for 10 sec.
#	8. Read the amount of log blocks written from the header of the
#		L2ARC device.
#	9. Offline the L2ARC device.
#	10. Online the L2ARC device.
#	11. Read the amount of log blocks rebuilt in arcstats and compare to
#		(8).
#	12. Check if the amount of log blocks on the cache device has
#		increased.
#	13. Export the pool.
#	14. Read the amount of log blocks on the cache device.
#	15. Import the pool.
#	16. Read the amount of log blocks rebuilt in arcstats and compare to
#		(14).
#	17. Check if the labels of the L2ARC device are intact.
#

verify_runnable "global"

log_assert "Off/onlining an L2ARC device restores all written blocks , vdev present."

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	log_must set_tunable32 L2ARC_NOPREFETCH $noprefetch
}
log_onexit cleanup

# L2ARC_NOPREFETCH is set to 0 to let L2ARC handle prefetches
typeset noprefetch=$(get_tunable L2ARC_NOPREFETCH)
log_must set_tunable32 L2ARC_NOPREFETCH 0

typeset fill_mb=400
typeset cache_sz=$(( 3 * $fill_mb ))
export FILE_SIZE=$(( floor($fill_mb / $NUMJOBS) ))M

log_must truncate -s ${cache_sz}M $VDEV_CACHE

log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE

log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/random_reads.fio

arcstat_quiescence_noecho l2_size
log_must zpool offline $TESTPOOL $VDEV_CACHE
arcstat_quiescence_noecho l2_size

typeset l2_dh_log_blk1=$(zdb -l $VDEV_CACHE | grep log_blk_count | \
	awk '{print $2}')
typeset l2_rebuild_log_blk_start=$(get_arcstat l2_rebuild_log_blks)

log_must zpool online $TESTPOOL $VDEV_CACHE
arcstat_quiescence_noecho l2_size

typeset l2_rebuild_log_blk_end=$(arcstat_quiescence_echo l2_rebuild_log_blks)

log_must test $l2_dh_log_blk1 -eq $(( $l2_rebuild_log_blk_end - \
	$l2_rebuild_log_blk_start ))
log_must test $l2_dh_log_blk1 -gt 0

log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/random_reads.fio

arcstat_quiescence_noecho l2_size
log_must zpool offline $TESTPOOL $VDEV_CACHE
arcstat_quiescence_noecho l2_size

typeset l2_dh_log_blk2=$(zdb -l $VDEV_CACHE | grep log_blk_count | \
	awk '{print $2}')
typeset l2_rebuild_log_blk_start=$(get_arcstat l2_rebuild_log_blks)

log_must zpool online $TESTPOOL $VDEV_CACHE
arcstat_quiescence_noecho l2_size

typeset l2_rebuild_log_blk_end=$(arcstat_quiescence_echo l2_rebuild_log_blks)

log_must test $l2_dh_log_blk2 -eq $(( $l2_rebuild_log_blk_end - \
	$l2_rebuild_log_blk_start ))
log_must test $l2_dh_log_blk2 -gt $l2_dh_log_blk1

log_must zpool export $TESTPOOL
arcstat_quiescence_noecho l2_feeds

typeset l2_dh_log_blk3=$(zdb -l $VDEV_CACHE | grep log_blk_count | \
	awk '{print $2}')
typeset l2_rebuild_log_blk_start=$(get_arcstat l2_rebuild_log_blks)

log_must zpool import -d $VDIR $TESTPOOL
arcstat_quiescence_noecho l2_size

typeset l2_rebuild_log_blk_end=$(arcstat_quiescence_echo l2_rebuild_log_blks)

log_must test $l2_dh_log_blk3 -eq $(( $l2_rebuild_log_blk_end - \
	$l2_rebuild_log_blk_start ))
log_must test $l2_dh_log_blk3 -gt 0

log must zpool offline $TESTPOOL $VDEV_CACHE
arcstat_quiescence_noecho l2_size

log_must zdb -lq $VDEV_CACHE

log_must zpool destroy -f $TESTPOOL

log_pass "Off/onlining an L2ARC device restores all written blocks, vdev present."
