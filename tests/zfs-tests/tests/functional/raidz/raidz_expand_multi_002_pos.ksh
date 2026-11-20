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

#
# DESCRIPTION:
#	An expansion that adds several disks can be paused, exported and
#	imported, and its data stays readable from the old and the new
#	layout.  More disks are added than the parity could hide, so a
#	wrongly copied row would show up as repaired data.
#
# STRATEGY:
#	For each layout (parity, old width, disks added):
#	1. Create the raidz pool, fill it, record a checksum of every file.
#	2. Attach the new disks with the copy paused half way.
#	3. Verify the raidz_expansion_multi feature is "active" and the label
#	   records the old width.
#	4. Verify the files, export, import, verify the files again.
#	5. Let the copy finish.  Verify the feature is back to "enabled", the
#	   label records one width step per added disk and no old width.
#	6. Scrub: no checksum errors and nothing repaired.  Verify the files.
#

verify_runnable "global"

typeset -r dev_size_mb=256
typeset -a layouts=("1 3 3" "2 4 4" "3 5 4")
typeset -A sums

function cleanup
{
	poolexists "$TESTPOOL" && zpool status -v "$TESTPOOL"
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	log_must rm -f $TEST_BASE_DIR/dev-*

	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
}

function record_sums
{
	for f in /$TESTPOOL/fs/*/*; do
		sums[$f]=$(xxh128digest $f)
	done
}

function verify_sums
{
	typeset -i n=0
	for f in "${!sums[@]}"; do
		if [[ "$(xxh128digest $f)" != "${sums[$f]}" ]]; then
			log_fail "checksum of $f changed"
		fi
		(( n = n + 1 ))
	done
	if [[ $n -eq 0 ]]; then
		log_fail "no files were checked"
	fi
}

function check_feature # <state>
{
	typeset state=$(zpool get -H -o value \
	    feature@raidz_expansion_multi $TESTPOOL)
	if [[ "$state" != "$1" ]]; then
		log_fail "raidz_expansion_multi is $state, expected $1"
	fi
}

log_onexit cleanup

for layout in "${layouts[@]}"; do
	set -- $layout
	typeset -i parity=$1 width=$2 added=$3
	typeset -i total=$((width + added))
	typeset -a disks=()
	typeset -a newdisks=()

	log_note "raidz$parity of $width, attaching $added disks, paused"

	for i in {1..$total}; do
		device=$TEST_BASE_DIR/dev-$i
		log_must truncate -s ${dev_size_mb}M $device
		if [[ $i -le $width ]]; then
			disks[${#disks[*]}+1]=$device
		else
			newdisks[${#newdisks[*]}+1]=$device
		fi
	done

	log_must zpool create -f -o cachefile=none $TESTPOOL \
	    raidz$parity "${disks[@]}"
	log_must zfs create -o recordsize=128k $TESTPOOL/fs
	log_must fill_fs /$TESTPOOL/fs 1 64 1048576 1 R
	record_sums

	reflow_size=$(get_pool_prop allocated $TESTPOOL)
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES \
	    $((reflow_size / 2))
	log_must zpool attach $TESTPOOL raidz$parity-0 "${newdisks[@]}"
	wait_raidz_expand_paused $TESTPOOL

	check_feature active
	log_must eval "zdb -l ${disks[1]} | \
	    grep -q 'raidz_expand_old_width: $width\$'"

	verify_sums
	log_must_busy zpool export $TESTPOOL
	log_must zpool import -o cachefile=none -d $TEST_BASE_DIR $TESTPOOL
	log_must check_pool_status $TESTPOOL "expand" "in progress"
	verify_sums

	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
	log_must zpool wait -t raidz_expand $TESTPOOL

	check_feature enabled
	typeset -i steps=$(zdb -l ${disks[1]} | \
	    grep "raidz_expand_txgs\[" | sort -u | wc -l)
	if [[ $steps -ne $added ]]; then
		log_fail "expected $added width steps in the label, found $steps"
	fi
	log_mustnot eval "zdb -l ${disks[1]} | grep -q raidz_expand_old_width"

	is_pool_scrubbing $TESTPOOL && wait_scrubbed $TESTPOOL
	verify_pool $TESTPOOL
	log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	verify_sums

	log_must zpool destroy $TESTPOOL
	log_must rm -f $TEST_BASE_DIR/dev-*
done

log_pass "a paused several-disk raidz expansion survives export and import"
