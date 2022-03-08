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
#	l2arc_mfuonly does not cache MRU buffers
#
# STRATEGY:
#	1. Set l2arc_mfuonly=yes
#	2. Create pool with a cache device.
#	3. Create a random file in that pool, smaller than the cache device
#		and random read for 10 sec.
#	4. Export and re-import the pool. This is necessary as some MFU ghost
#		buffers with prefetch status may transition to MRU eventually.
#		By re-importing the pool the l2 arcstats reflect the ARC state
#		of L2ARC buffers upon their caching in L2ARC.
#	5. Verify l2arc_mru_asize is 0.
#

verify_runnable "global"

command -v fio > /dev/null || log_unsupported "fio missing"

log_assert "l2arc_mfuonly does not cache MRU buffers."

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	log_must set_tunable32 L2ARC_NOPREFETCH $noprefetch
	log_must set_tunable32 L2ARC_MFUONLY $mfuonly
	log_must set_tunable32 PREFETCH_DISABLE $zfsprefetch
}
log_onexit cleanup

# L2ARC_NOPREFETCH is set to 1 as some prefetched buffers may
# transition to MRU.
typeset noprefetch=$(get_tunable L2ARC_NOPREFETCH)
log_must set_tunable32 L2ARC_NOPREFETCH 1

typeset mfuonly=$(get_tunable L2ARC_MFUONLY)
log_must set_tunable32 L2ARC_MFUONLY 1

typeset zfsprefetch=$(get_tunable PREFETCH_DISABLE)
log_must set_tunable32 PREFETCH_DISABLE 1

typeset fill_mb=800
typeset cache_sz=$(( 1.4 * $fill_mb ))
export FILE_SIZE=$(( floor($fill_mb / $NUMJOBS) ))M

log_must truncate -s ${cache_sz}M $VDEV_CACHE

typeset log_blk_start=$(get_arcstat l2_log_blk_writes)

log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE

log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/random_reads.fio

log_must zpool export $TESTPOOL
log_must zpool import -d $VDIR $TESTPOOL

# Regardless of l2arc_noprefetch, some MFU buffers might be evicted
# from ARC, accessed later on as prefetches and transition to MRU as
# prefetches.
# If accessed again they are counted as MRU and the l2arc_mru_asize arcstat
# will not be 0 (mentioned also in zfs.4)
# For the purposes of this test we mitigate this by disabling (predictive)
# ZFS prefetches with zfs_prefetch_disable=1.
log_must test $(get_arcstat l2_mru_asize) -eq 0

log_must zpool destroy -f $TESTPOOL

log_pass "l2arc_mfuonly does not cache MRU buffers."
