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
#	Persistent L2ARC with an unencrypted ZFS file system succeeds
#
# STRATEGY:
#	1. Create pool with a cache device.
#	2. Export and re-import pool without writing any data.
#	3. Create a random file in that pool and random read for 10 sec.
#	4. Export pool.
#	5. Read the amount of log blocks written from the header of the
#		L2ARC device.
#	6. Import pool.
#	7. Read the amount of log blocks rebuilt in arcstats and compare to
#		(5).
#	8. Check if the labels of the L2ARC device are intact.
#
#	* We can predict the minimum bytes of L2ARC restored if we subtract
#	from the effective size of the cache device the bytes l2arc_evict()
#	evicts:
#	l2: L2ARC device size - VDEV_LABEL_START_SIZE - l2ad_dev_hdr_asize
#	wr_sz: l2arc_write_max + l2arc_write_boost (worst case)
#	blk_overhead: wr_sz / SPA_MINBLOCKSIZE / (l2 / SPA_MAXBLOCKSIZE) *
#		sizeof (l2arc_log_blk_phys_t)
#	min restored size: l2 - (wr_sz + blk_overhead)
#

verify_runnable "global"

command -v fio > /dev/null || log_unsupported "fio missing"

log_assert "Persistent L2ARC with an unencrypted ZFS file system succeeds."

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	log_must set_tunable32 L2ARC_NOPREFETCH $noprefetch
	log_must set_tunable32 L2ARC_REBUILD_BLOCKS_MIN_L2SIZE \
		$rebuild_blocks_min_l2size
}
log_onexit cleanup

# L2ARC_NOPREFETCH is set to 0 to let L2ARC handle prefetches
typeset noprefetch=$(get_tunable L2ARC_NOPREFETCH)
typeset rebuild_blocks_min_l2size=$(get_tunable L2ARC_REBUILD_BLOCKS_MIN_L2SIZE)
log_must set_tunable32 L2ARC_NOPREFETCH 0
log_must set_tunable32 L2ARC_REBUILD_BLOCKS_MIN_L2SIZE 0

typeset fill_mb=800
typeset cache_sz=$(( floor($fill_mb / 2) ))
export FILE_SIZE=$(( floor($fill_mb / $NUMJOBS) ))M

log_must truncate -s ${cache_sz}M $VDEV_CACHE

log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE

log_must zpool export $TESTPOOL
log_must zpool import -d $VDIR $TESTPOOL

log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/random_reads.fio

arcstat_quiescence_noecho l2_size
log_must zpool export $TESTPOOL
arcstat_quiescence_noecho l2_feeds

typeset l2_dh_log_blk=$(zdb -l $VDEV_CACHE | awk '/log_blk_count/ {print $2}')

typeset l2_rebuild_log_blk_start=$(get_arcstat l2_rebuild_log_blks)

log_must zpool import -d $VDIR $TESTPOOL
arcstat_quiescence_noecho l2_size

typeset l2_rebuild_log_blk_end=$(arcstat_quiescence_echo l2_rebuild_log_blks)

log_must test $l2_dh_log_blk -eq $(( $l2_rebuild_log_blk_end -
	$l2_rebuild_log_blk_start ))
log_must test $l2_dh_log_blk -gt 0

log_must zpool offline $TESTPOOL $VDEV_CACHE
arcstat_quiescence_noecho l2_size

log_must zdb -lllq $VDEV_CACHE

log_must zpool destroy -f $TESTPOOL

log_pass "Persistent L2ARC with an unencrypted ZFS file system succeeds."
