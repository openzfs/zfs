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

#
# Copyright 2020 Richard Elling
#

. $STF_SUITE/include/libtest.shlib

typeset tmpfile=$TEST_BASE_DIR/zpool_influxdb.out.$$
function cleanup
{
	if [[ -f $tmpfile ]]; then
		rm -f $tmpfile
	fi
}
log_onexit cleanup

log_assert "zpool_influxdb gathers statistics"

if ! is_global_zone ; then
	TESTPOOL=${TESTPOOL%%/*}
fi

function check_for
{
    log_must grep -q "^${1}," $tmpfile
}

# by default, all stats and histograms for all pools
log_must eval "zpool_influxdb > $tmpfile"

STATS="
zpool_io_size
zpool_latency
zpool_stats
zpool_vdev_queue
zpool_vdev_stats
"
for stat in $STATS; do
    check_for $stat
done

# scan stats aren't expected to be there until after a scan has started
log_must zpool scrub $TESTPOOL
log_must eval "zpool_influxdb > $tmpfile"
check_for zpool_scan_stats

log_pass "zpool_influxdb gathers statistics"
