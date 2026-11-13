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
#	'zpool attach <pool> raidzP-0 <dev> <dev> ...' adds several disks
#	to a raidz vdev in one expansion.
#
# STRATEGY:
#	For each layout (parity, old width, disks added):
#	1. Create the raidz pool and fill it with random data, recording
#	   a checksum of every file.
#	2. Attach all the new disks with one 'zpool attach -w'.
#	3. Verify the new width, that the raidz_expansion_multi feature is
#	   back to "enabled", and that the label records one width step
#	   per added disk.
#	4. Scrub: no checksum errors and nothing repaired.
#	5. Verify every file checksum.
#

verify_runnable "global"

typeset -r dev_size_mb=256
typeset -a layouts=("1 3 2" "1 5 3" "2 6 3" "3 7 2")
typeset -A sums

function cleanup
{
	poolexists "$TESTPOOL" && zpool status -v "$TESTPOOL"
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	log_must rm -f $TEST_BASE_DIR/dev-*
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

log_onexit cleanup

for layout in "${layouts[@]}"; do
	set -- $layout
	typeset -i parity=$1 width=$2 added=$3
	typeset -i total=$((width + added))
	typeset -a disks=()
	typeset -a newdisks=()

	log_note "raidz$parity of $width, attaching $added disks at once"

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

	log_must zpool attach -w $TESTPOOL raidz$parity-0 "${newdisks[@]}"

	typeset -i found=$(zpool status $TESTPOOL | \
	    grep -c "$TEST_BASE_DIR/dev-")
	if [[ $found -ne $total ]]; then
		log_fail "expected $total disks after the attach, found $found"
	fi

	state=$(zpool get -H -o value feature@raidz_expansion_multi $TESTPOOL)
	if [[ "$state" != "enabled" ]]; then
		log_fail "raidz_expansion_multi is $state after the expansion"
	fi

	typeset -i steps=$(zdb -l ${disks[1]} | \
	    grep "raidz_expand_txgs\[" | sort -u | wc -l)
	if [[ $steps -ne $added ]]; then
		log_fail "expected $added width steps in the label, found $steps"
	fi

	is_pool_scrubbing $TESTPOOL && wait_scrubbed $TESTPOOL
	verify_pool $TESTPOOL
	log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	verify_sums

	log_must zpool destroy $TESTPOOL
	log_must rm -f $TEST_BASE_DIR/dev-*
done

log_pass "several disks attached to a raidz vdev in one expansion"
