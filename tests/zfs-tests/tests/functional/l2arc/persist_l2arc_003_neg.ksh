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
#	Persistent L2ARC fails as expected when L2ARC_REBUILD_ENABLED = 0
#
# STRATEGY:
#	1. Set L2ARC_REBUILD_ENABLED = 0
#	2. Create pool with a cache device.
#	3. Create a random file in that pool and random read for 10 sec.
#	4. Export pool.
#	5. Import pool.
#	6. Check in zpool iostat if the cache device has space allocated.
#	7. Read the file written in (3) and check if arcstats.l2_hits increased.
#

verify_runnable "global"

command -v fio > /dev/null || log_unsupported "fio missing"

log_assert "Persistent L2ARC fails as expected when L2ARC_REBUILD_ENABLED = 0."

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	log_must set_tunable32 L2ARC_REBUILD_ENABLED $rebuild_enabled
	log_must set_tunable32 L2ARC_NOPREFETCH $noprefetch
}
log_onexit cleanup

# L2ARC_NOPREFETCH is set to 0 to let L2ARC handle prefetches
typeset noprefetch=$(get_tunable L2ARC_NOPREFETCH)
log_must set_tunable32 L2ARC_NOPREFETCH 0

# disable L2ARC rebuild
typeset rebuild_enabled=$(get_tunable L2ARC_REBUILD_ENABLED)
log_must set_tunable32 L2ARC_REBUILD_ENABLED 0

typeset fill_mb=800
typeset cache_sz=$(( 2 * $fill_mb ))
export FILE_SIZE=$(( floor($fill_mb / $NUMJOBS) ))M

log_must truncate -s ${cache_sz}M $VDEV_CACHE

log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE

log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/random_reads.fio

log_must zpool export $TESTPOOL

typeset l2_success_start=$(kstat arcstats.l2_rebuild_success)

log_must zpool import -d $VDIR $TESTPOOL
log_mustnot test "$(zpool iostat -Hpv $TESTPOOL $VDEV_CACHE | awk '{print $2}')" -gt 80000000

typeset l2_success_end=$(kstat arcstats.l2_rebuild_success)

log_mustnot test $l2_success_end -gt $l2_success_start

log_must zpool destroy -f $TESTPOOL
log_must set_tunable32 L2ARC_REBUILD_ENABLED $rebuild_enabled

log_pass "Persistent L2ARC fails as expected when L2ARC_REBUILD_ENABLED = 0."
