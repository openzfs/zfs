#!/bin/sh
# SPDX-License-Identifier: CDDL-1.0

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

#
# Copyright (c) 2016 by Intel, Corp.
#

#
# Linux platform placeholder for collecting prefetch I/O stats
# TBD if we can add additional kstats to achieve the desired results
#

getstat() {
	awk -v c="$1" '$1 == c {print $3; exit}' /proc/spl/kstat/zfs/arcstats
}

get_prefetch_ios() {
	echo $(( $(getstat prefetch_data_misses) + $(getstat prefetch_metadata_misses) ))
}

if [ $# -ne 2 ]
then
	echo "Usage: ${0##*/} poolname interval" >&2
	exit 1
fi

interval=$2
prefetch_ios=$(get_prefetch_ios)
prefetched_demand_reads=$(getstat demand_hit_predictive_prefetch)
async_upgrade_sync=$(getstat async_upgrade_sync)

while true
do
	new_prefetch_ios=$(get_prefetch_ios)
	printf '%u\n%-24s\t%u\n' "$(date +%s)" "prefetch_ios" \
	    $(( new_prefetch_ios - prefetch_ios ))
	prefetch_ios=$new_prefetch_ios

	new_prefetched_demand_reads=$(getstat demand_hit_predictive_prefetch)
	printf '%-24s\t%u\n' "prefetched_demand_reads" \
	    $(( new_prefetched_demand_reads - prefetched_demand_reads ))
	prefetched_demand_reads=$new_prefetched_demand_reads

	new_async_upgrade_sync=$(getstat async_upgrade_sync)
	printf '%-24s\t%u\n' "async_upgrade_sync" \
	    $(( new_async_upgrade_sync - async_upgrade_sync ))
	async_upgrade_sync=$new_async_upgrade_sync

	sleep "$interval"
done
