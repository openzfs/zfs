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
#	L2ARC MFU/MRU arcstats do not leak
#
# STRATEGY:
#	1. Create pool with a cache device.
#	2. Create a random file in that pool, smaller than the cache device
#		and random read for 10 sec.
#	3. Read l2arc_mfu_asize and l2arc_mru_asize
#	4. Export pool.
#	5. Verify l2arc_mfu_asize and l2arc_mru_asize are 0.
#	6. Import pool.
#	7. Read random read for 10 sec.
#	8. Read l2arc_mfu_asize and l2arc_mru_asize
#	9. Verify that L2ARC MFU increased and MFU+MRU = L2_asize.
#

verify_runnable "global"

log_assert "L2ARC MFU/MRU arcstats do not leak."

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
typeset cache_sz=$(( 1.4 * $fill_mb ))
export FILE_SIZE=$(( floor($fill_mb / $NUMJOBS) ))M

log_must truncate -s ${cache_sz}M $VDEV_CACHE

log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE

log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/random_reads.fio

arcstat_quiescence_noecho l2_size
log_must zpool offline $TESTPOOL $VDEV_CACHE
arcstat_quiescence_noecho l2_size

typeset l2_mfu_init=$(get_arcstat l2_mfu_asize)
typeset l2_mru_init=$(get_arcstat l2_mru_asize)
typeset l2_prefetch_init=$(get_arcstat l2_prefetch_asize)
typeset l2_asize_init=$(get_arcstat l2_asize)

log_must zpool online $TESTPOOL $VDEV_CACHE
arcstat_quiescence_noecho l2_size
log_must zpool export $TESTPOOL
arcstat_quiescence_noecho l2_feeds

log_must test $(get_arcstat l2_mfu_asize) -eq 0
log_must test $(get_arcstat l2_mru_asize) -eq 0
log_must zpool import -d $VDIR $TESTPOOL
arcstat_quiescence_noecho l2_size

log_must fio $FIO_SCRIPTS/random_reads.fio
arcstat_quiescence_noecho l2_size
log_must zpool offline $TESTPOOL $VDEV_CACHE
arcstat_quiescence_noecho l2_size

typeset l2_mfu_end=$(get_arcstat l2_mfu_asize)
typeset l2_mru_end=$(get_arcstat l2_mru_asize)
typeset l2_prefetch_end=$(get_arcstat l2_prefetch_asize)
typeset l2_asize_end=$(get_arcstat l2_asize)

log_must test $(( $l2_mru_end + $l2_mfu_end + $l2_prefetch_end - \
	$l2_asize_end )) -eq 0
log_must test $(( $l2_mru_init + $l2_mfu_init + $l2_prefetch_init - \
	$l2_asize_init )) -eq 0

log_must zpool destroy -f $TESTPOOL

log_pass "L2ARC MFU/MRU arcstats do not leak."
