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
# Copyright (c) 2026 by Skountz. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify that blocks allocated in open context while
#	raidz_expansion_accounting is activated are freed with the
#	deflation ratio they were charged with.
#
#	Synchronous writes make dmu_sync() allocate their blocks in open
#	context, for txgs that have not started syncing yet.  When the
#	feature is activated while such blocks exist, they must stay on
#	the legacy ratio, or freeing them removes accounting excess that
#	was never added (which debug builds assert on).
#
# STRATEGY:
#	1. Create a RAIDZ1 pool of 3 disks with the feature disabled
#	2. Expand it to 4 disks
#	3. Enable the feature while synchronous writers are running, and
#	   stop them shortly after
#	4. Delete the files, export and import, and sync
#	5. Verify the dataset's used space is back to its baseline
#	6. Repeat from step 1
#

verify_runnable "global"

typeset -r devs=4
typeset -r dev_size_mb=768
typeset -r writers=4
typeset -r file_mb=192
typeset -r rounds=3
typeset -r pool=$TESTPOOL
typeset -r fs=$pool/fs

typeset -a disks
typeset -a pids

function cleanup
{
	for pid in ${pids[@]}; do
		kill $pid 2>/dev/null
	done
	wait
	poolexists "$pool" && log_must_busy zpool destroy "$pool"

	for i in {1..$devs}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done
}

log_onexit cleanup

for i in {1..$devs}; do
	disks[$i]=$TEST_BASE_DIR/dev-$i
done

for round in {1..$rounds}; do
	for i in {1..$devs}; do
		log_must truncate -s ${dev_size_mb}M ${disks[$i]}
	done

	log_must zpool create -f -o cachefile=none \
	    -o feature@raidz_expansion_accounting=disabled \
	    $pool raidz1 ${disks[1..3]}
	log_must zfs create -o compression=off -o recordsize=128k \
	    -o logbias=throughput -o sync=always $fs

	log_must zpool attach -w $pool raidz1-0 ${disks[4]}
	log_must zpool sync $pool
	baseline=$(get_prop used $fs)

	pids=()
	for w in {1..$writers}; do
		dd if=/dev/urandom of=/$fs/file$w bs=128k \
		    count=$((file_mb * 8)) 2>/dev/null &
		pids+=($!)
	done
	sleep 1
	log_must zpool set feature@raidz_expansion_accounting=enabled $pool
	sleep 1
	typeset -i running=0
	for pid in ${pids[@]}; do
		kill $pid 2>/dev/null && running=running+1
	done
	wait
	pids=()
	log_note "Round $round: $running of $writers writers ran past activation"

	feat_status=$(get_pool_prop feature@raidz_expansion_accounting $pool)
	if [[ "$feat_status" != "active" ]]; then
		log_fail "Feature should be active (got: $feat_status)"
	fi

	log_must rm /$fs/file*
	log_must zpool sync $pool
	log_must zpool export $pool
	log_must zpool import -d $TEST_BASE_DIR $pool
	log_must zpool sync $pool

	typeset -i tolerance=$((32 * 1024))
	used=$(get_prop used $fs)
	log_note "Round $round: used $used (baseline $baseline)"
	if (( used < baseline - tolerance || used > baseline + tolerance ))
	then
		log_fail "Dataset used ($used) differs from baseline" \
		    "($baseline) by more than $tolerance bytes"
	fi

	log_must_busy zpool destroy $pool
done

log_pass "Blocks allocated in open context around the feature" \
    "activation are freed consistently."
