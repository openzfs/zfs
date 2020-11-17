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
#	Persistent L2ARC restores all written log blocks
#
# STRATEGY:
#	1. Create pool with a cache device.
#	2. Create a random file in that pool, smaller than the cache device
#		and random read for 10 sec.
#	3. Export pool.
#	4. Read amount of log blocks written.
#	5. Import pool.
#	6. Read amount of log blocks built.
#	7. Compare the two amounts.
#	8. Read the file written in (2) and check if l2_hits in
#		/proc/spl/kstat/zfs/arcstats increased.
#	9. Check if the labels of the L2ARC device are intact.
#

verify_runnable "global"

log_assert "Persistent L2ARC restores all written log blocks."

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

typeset fill_mb=800
typeset cache_sz=$(( 2 * $fill_mb ))
export FILE_SIZE=$(( floor($fill_mb / $NUMJOBS) ))M

log_must truncate -s ${cache_sz}M $VDEV_CACHE

typeset log_blk_start=$(get_arcstat l2_log_blk_writes)

log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE

log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/random_reads.fio

arcstat_quiescence_noecho l2_size
log_must zpool export $TESTPOOL
arcstat_quiescence_noecho l2_feeds

typeset log_blk_end=$(get_arcstat l2_log_blk_writes)
typeset log_blk_rebuild_start=$(get_arcstat l2_rebuild_log_blks)

log_must zpool import -d $VDIR $TESTPOOL

typeset l2_hits_start=$(get_arcstat l2_hits)

log_must fio $FIO_SCRIPTS/random_reads.fio
arcstat_quiescence_noecho l2_size

typeset log_blk_rebuild_end=$(arcstat_quiescence_echo l2_rebuild_log_blks)
typeset l2_hits_end=$(get_arcstat l2_hits)

log_must test $(( $log_blk_rebuild_end - $log_blk_rebuild_start )) -eq \
	$(( $log_blk_end - $log_blk_start ))

log_must test $l2_hits_end -gt $l2_hits_start

log_must zpool offline $TESTPOOL $VDEV_CACHE
arcstat_quiescence_noecho l2_size

log_must zdb -lq $VDEV_CACHE

log_must zpool destroy -f $TESTPOOL

log_pass "Persistent L2ARC restores all written log blocks."
