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
#	Verify that blocks born in the txg that enables
#	raidz_expansion_accounting on an already-expanded pool are freed
#	with the same deflation ratio they were charged with.
#
#	The feature is activated by a sync task, which runs after the
#	dirty datasets of that txg have been synced.  Blocks born in that
#	txg are therefore charged at the legacy ratio, and must also be
#	freed at the legacy ratio.  Otherwise the free releases more space
#	than was charged.
#
# STRATEGY:
#	1. Create a RAIDZ1 pool of 3 disks with the feature disabled
#	2. Expand it to 4 disks
#	3. Raise zfs_txg_timeout, write two files and enable the feature,
#	   so the data and the enable land in the same txg
#	4. Delete the first file and sync
#	5. Export and import, delete the second file and sync
#	6. Verify the dataset's used space is back to its baseline
#

verify_runnable "global"

typeset -r devs=4
typeset -r dev_size_mb=512
typeset -r file_mb=24
typeset -r pool=$TESTPOOL
typeset -r fs=$pool/fs

typeset -a disks

txg_timeout=$(get_tunable TXG_TIMEOUT)

function cleanup
{
	log_must set_tunable32 TXG_TIMEOUT $txg_timeout
	poolexists "$pool" && log_must_busy zpool destroy "$pool"

	for i in {1..$devs}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done
}

log_onexit cleanup

for i in {1..$devs}; do
	disks[$i]=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M ${disks[$i]}
done

log_must zpool create -f -o cachefile=none \
    -o feature@raidz_expansion_accounting=disabled \
    $pool raidz1 ${disks[1..3]}
log_must zfs create -o compression=off -o recordsize=128k $fs

log_must zpool attach -w $pool raidz1-0 ${disks[4]}
log_must zpool sync $pool

baseline=$(get_prop used $fs)
log_note "Baseline used: $baseline"

# Write the files and enable the feature in the same txg.
log_must set_tunable32 TXG_TIMEOUT 600
log_must zpool sync $pool
log_must dd if=/dev/urandom of=/$fs/file1 bs=1M count=$file_mb
log_must dd if=/dev/urandom of=/$fs/file2 bs=1M count=$file_mb
log_must zpool set feature@raidz_expansion_accounting=enabled $pool
log_must set_tunable32 TXG_TIMEOUT $txg_timeout

feat_status=$(get_pool_prop feature@raidz_expansion_accounting $pool)
if [[ "$feat_status" != "active" ]]; then
	log_fail "Feature should be active (got: $feat_status)"
fi

log_must rm /$fs/file1
log_must zpool sync $pool

log_must zpool export $pool
log_must zpool import -d $TEST_BASE_DIR $pool

log_must rm /$fs/file2
log_must zpool sync $pool

#
# Allow for small metadata changes.  A born/free mismatch frees about
# 120K more than was charged for each file, so used would end up well
# below the baseline.
#
typeset -ri tolerance=$((32 * 1024))
used=$(get_prop used $fs)
log_note "Used after deletes: $used (baseline $baseline)"
if (( used < baseline - tolerance || used > baseline + tolerance )); then
	log_fail "Dataset used ($used) differs from baseline ($baseline)" \
	    "by more than $tolerance bytes"
fi

log_pass "Blocks born in the feature enabling txg are freed consistently."
