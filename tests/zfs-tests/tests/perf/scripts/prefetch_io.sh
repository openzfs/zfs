#!/usr/bin/env bash

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

zfs_kstats="/proc/spl/kstat/zfs"

function get_prefetch_ios
{
        typeset -l data_misses=`awk '$1 == "prefetch_data_misses" \
            { print $3 }' $zfs_kstats/arcstats`
        typeset -l metadata_misses=`awk '$1 == "prefetch_metadata_misses" \
            { print $3 }' $zfs_kstats/arcstats`
        typeset -l total_misses=$(( $data_misses + $metadata_misses ))

        echo $total_misses
}

function get_prefetched_demand_reads
{
	typeset -l demand_reads=`awk '$1 == "demand_hit_predictive_prefetch" \
	    { print $3 }' $zfs_kstats/arcstats`

	echo $demand_reads
}

function get_async_upgrade_sync
{
	typeset -l sync_wait=`awk '$1 == "async_upgrade_sync" \
	    { print $3 }' $zfs_kstats/arcstats`

	echo $sync_wait
}

if [ $# -ne 2 ]
then
	echo "Usage: `basename $0` <poolname> interval" >&2
	exit 1
fi

poolname=$1
interval=$2
prefetch_ios=$(get_prefetch_ios)
prefetched_demand_reads=$(get_prefetched_demand_reads)
async_upgrade_sync=$(get_async_upgrade_sync)

while true
do
	new_prefetch_ios=$(get_prefetch_ios)
	printf "%u\n%-24s\t%u\n" $(date +%s) "prefetch_ios" \
	    $(( $new_prefetch_ios - $prefetch_ios ))
	prefetch_ios=$new_prefetch_ios

	new_prefetched_demand_reads=$(get_prefetched_demand_reads)
	printf "%-24s\t%u\n" "prefetched_demand_reads" \
	    $(( $new_prefetched_demand_reads - $prefetched_demand_reads ))
	prefetched_demand_reads=$new_prefetched_demand_reads

	new_async_upgrade_sync=$(get_async_upgrade_sync)
	printf "%-24s\t%u\n" "async_upgrade_sync" \
	    $(( $new_async_upgrade_sync - $async_upgrade_sync ))
	async_upgrade_sync=$new_async_upgrade_sync

	sleep $interval
done
