#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at
# https://opensource.org/licenses/CDDL-1.0
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
