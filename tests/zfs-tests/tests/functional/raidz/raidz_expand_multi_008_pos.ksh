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
#	Corner cases right after a several-disk expansion:
#	- An expansion of an empty pool by 8 disks, then at once by 1 more,
#	  records 9 width steps, each at a later txg than the one before,
#	  and the 8 steps of the first expansion at consecutive txgs.
#	- A checkpoint taken as soon as a several-disk expansion has
#	  completed does not damage the pool, and the pool can be rewound to
#	  it.  The checkpoint is refused only while the expansion is still
#	  recorded as running ("raidz expansion in progress"); the few txgs
#	  in which raidz_expansion_multi is still active after that do not
#	  refuse it.  Which of the two happens depends on timing, so the test
#	  records it and checks the pool either way.
#
# STRATEGY:
#	1. Create a raidz1 of 3 with no data.  "zpool attach -w" 8 disks,
#	   then right away "zpool attach -w" 1 more.
#	2. The pool is 12 wide, raidz_expansion_multi is "enabled", and the
#	   labels record 9 width steps in increasing txgs, the first 8 at
#	   consecutive txgs.  Export, import: the same again.  Scrub.
#	3. Create a raidz1 of 3 with some data and attach 2 disks.  As soon
#	   as "zpool status" shows the expansion completed, take a
#	   checkpoint (if refused, wait for the expansion and take it then).
#	4. Wait for the expansion: raidz_expansion_multi is "enabled".
#	   Scrub, verify the files.  Export, rewind to the checkpoint: the
#	   pool is 5 wide with 2 width steps, raidz_expansion_multi ends up
#	   "enabled", a scrub repairs nothing and the files are intact.
#

verify_runnable "global"

typeset -r dev_size_mb=128
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

function check_width # <width>
{
	typeset -i found=$(zpool status $TESTPOOL | \
	    grep -c "$TEST_BASE_DIR/dev-")
	if [[ $found -ne $1 ]]; then
		log_fail "expected $1 disks in $TESTPOOL, found $found"
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

#
# Print the txgs of the width steps recorded in the label of a device, in
# order.  zdb -l prints them as "raidz_expand_txgs[<i>]: <txg>".
#
function label_steps # <device>
{
	zdb -l $1 | awk -F '[][]' '/raidz_expand_txgs\[/ {
		v = $3; sub(/^: */, "", v); txg[$2 + 0] = v
		if ($2 + 1 > n) n = $2 + 1
	    } END { for (i = 0; i < n; i++) print txg[i] }'
}

#
# Check that the label of a device records <count> width steps, each at a
# later txg than the one before, and that the first <consecutive> of them
# are at consecutive txgs.
#
function check_steps # <device> <count> <consecutive>
{
	typeset -a steps=($(label_steps $1))
	typeset -i i

	log_note "width steps in the label of $1: ${steps[*]}"
	if (( ${#steps[@]} != $2 )); then
		log_fail "expected $2 width steps, found ${#steps[@]}"
	fi
	for (( i = 1; i < ${#steps[@]}; i++ )); do
		if (( steps[i] <= steps[i - 1] )); then
			log_fail "width step $i is not after step $((i - 1))"
		fi
		if (( i < $3 && steps[i] != steps[i - 1] + 1 )); then
			log_fail "width steps $((i - 1)) and $i are not at" \
			    "consecutive txgs"
		fi
	done
}

log_onexit cleanup

log_assert "a several-disk raidz expansion can be followed right away by" \
    "another attach or a checkpoint"

for i in {1..12}; do
	log_must truncate -s ${dev_size_mb}M $TEST_BASE_DIR/dev-$i
done
d=$TEST_BASE_DIR/dev-

# 1. An empty pool, plus 8, then plus 1
log_must zpool create -f -o cachefile=none $TESTPOOL raidz1 ${d}1 ${d}2 ${d}3
log_must zpool attach -w $TESTPOOL raidz1-0 \
    ${d}4 ${d}5 ${d}6 ${d}7 ${d}8 ${d}9 ${d}10 ${d}11
log_must zpool attach -w $TESTPOOL raidz1-0 ${d}12
# A one-disk expansion does not activate raidz_expansion_multi, so -w can
# return before the txg that writes its width step to the labels has synced.
sync_pool $TESTPOOL

# 2. Widths and steps, before and after export and import
check_width 12
check_feature enabled
check_steps ${d}1 9 8
check_steps ${d}12 9 8
log_must_busy zpool export $TESTPOOL
check_steps ${d}1 9 8
log_must zpool import -o cachefile=none -d $TEST_BASE_DIR $TESTPOOL
check_width 12
check_feature enabled
check_steps ${d}1 9 8
log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=1M count=16
is_pool_scrubbing $TESTPOOL && wait_scrubbed $TESTPOOL
verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"
log_must zpool destroy $TESTPOOL

# 3. A checkpoint as soon as the expansion has completed
log_must zpool create -f -o cachefile=none $TESTPOOL raidz1 ${d}1 ${d}2 ${d}3
log_must zfs create $TESTPOOL/fs
log_must fill_fs /$TESTPOOL/fs 1 16 1048576 1 R
record_sums
log_must zpool attach $TESTPOOL raidz1-0 ${d}4 ${d}5
typeset -i t0=$SECONDS
while ! check_pool_status $TESTPOOL "expand" "expanded"; do
	if (( SECONDS - t0 > 300 )); then
		log_fail "the expansion did not complete in 300 seconds"
	fi
	sleep 1
done
state=$(zpool get -H -o value feature@raidz_expansion_multi $TESTPOOL)
if out=$(zpool checkpoint $TESTPOOL 2>&1); then
	log_note "checkpoint taken; raidz_expansion_multi was $state just" \
	    "before"
else
	log_note "checkpoint refused: $out"
	if [[ "$out" != *"raidz expansion in progress"* ]]; then
		log_fail "unexpected checkpoint refusal: $out"
	fi
	log_must zpool wait -t raidz_expand $TESTPOOL
	log_must zpool checkpoint $TESTPOOL
fi

# 4. The pool, before and after a rewind to the checkpoint
log_must zpool wait -t raidz_expand $TESTPOOL
check_feature enabled
check_width 5
check_steps ${d}1 2 2
is_pool_scrubbing $TESTPOOL && wait_scrubbed $TESTPOOL
verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"
verify_sums

log_must_busy zpool export $TESTPOOL
log_must zpool import -o cachefile=none -d $TEST_BASE_DIR \
    --rewind-to-checkpoint $TESTPOOL
log_must zpool wait -t raidz_expand $TESTPOOL
check_feature enabled
check_width 5
check_steps ${d}1 2 2
is_pool_scrubbing $TESTPOOL && wait_scrubbed $TESTPOOL
verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"
verify_sums

log_pass "a several-disk raidz expansion can be followed right away by" \
    "another attach or a checkpoint"
