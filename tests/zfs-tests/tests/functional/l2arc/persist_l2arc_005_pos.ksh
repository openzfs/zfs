#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
#	Off/onlining an L2ARC device results in rebuilding L2ARC, vdev present.
#
# STRATEGY:
#	1. Create pool with a cache device.
#	2. Create a random file in that pool and random read for 10 sec.
#	3. Offline the L2ARC device.
#	4. Read the amount of log blocks written from the header of the
#		L2ARC device.
#	5. Online the L2ARC device.
#	6. Read the amount of log blocks rebuilt in arcstats and compare to
#		(4).
#	7. Check if the labels of the L2ARC device are intact.
#

verify_runnable "global"

command -v fio > /dev/null || log_unsupported "fio missing"

log_assert "Off/onlining an L2ARC device results in rebuilding L2ARC, vdev present."

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

log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/random_reads.fio

arcstat_quiescence_noecho l2_size
log_must zpool offline $TESTPOOL $VDEV_CACHE
arcstat_quiescence_noecho l2_size

typeset l2_rebuild_log_blk_start=$(kstat arcstats.l2_rebuild_log_blks)
typeset l2_dh_log_blk=$(zdb -l $VDEV_CACHE | awk '/log_blk_count/ {print $2}')

log_must zpool online $TESTPOOL $VDEV_CACHE
arcstat_quiescence_noecho l2_size

typeset l2_rebuild_log_blk_end=$(arcstat_quiescence_echo l2_rebuild_log_blks)

# Upon onlining the cache device we might write additional blocks to it
# before it is marked for rebuild as the l2ad_* parameters are not cleared
# when offlining the device. See comment in l2arc_rebuild_vdev().
# So we cannot compare the amount of rebuilt log blocks to the amount of log
# blocks read from the header of the device.
log_must test $(( $l2_rebuild_log_blk_end - \
	$l2_rebuild_log_blk_start )) -gt 0
log_must test $l2_dh_log_blk -gt 0

log_must zpool offline $TESTPOOL $VDEV_CACHE
arcstat_quiescence_noecho l2_size

log_must zdb -lq $VDEV_CACHE

log_must zpool destroy -f $TESTPOOL

log_pass "Off/onlining an L2ARC device results in rebuilding L2ARC, vdev present."
