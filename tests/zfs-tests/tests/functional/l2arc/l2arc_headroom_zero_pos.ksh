#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/l2arc/l2arc.cfg

#
# DESCRIPTION:
#	l2arc_headroom=0 removes the per-cycle headroom limit instead of
#	stopping L2ARC writes.
#
# STRATEGY:
#	1. Set l2arc_headroom=0.
#	2. Create a pool with a cache device.
#	3. Create random files in that pool, smaller than the cache device,
#		and random read them for 10 sec.
#	4. Verify that buffers are written to the cache device within
#		60 seconds.
#

verify_runnable "global"

command -v fio > /dev/null || log_unsupported "fio missing"

log_assert "l2arc_headroom=0 does not stop L2ARC writes."

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	log_must set_tunable64 L2ARC_HEADROOM $headroom
	log_must set_tunable32 L2ARC_NOPREFETCH $noprefetch
}
log_onexit cleanup

typeset headroom=$(get_tunable L2ARC_HEADROOM)
log_must set_tunable64 L2ARC_HEADROOM 0

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

# arcstat_quiescence never returns while l2_size is 0: give up after 60s.
typeset -i tries=0
while (( $(kstat arcstats.l2_size) == 0 && tries < 60 )); do
	sleep 1
	(( tries += 1 ))
done
typeset l2_size=$(kstat arcstats.l2_size)
log_note "l2_size=$l2_size"
log_must test $l2_size -gt 0

log_must zpool destroy -f $TESTPOOL

log_pass "l2arc_headroom=0 does not stop L2ARC writes."
