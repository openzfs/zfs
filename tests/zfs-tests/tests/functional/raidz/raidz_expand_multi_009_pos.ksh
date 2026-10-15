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
#	A several-disk expansion after an earlier one-disk expansion records
#	its width steps after the earlier one.  Dedup entries written at the
#	width in between (after the first expansion, before the second) are
#	still read at that width after the second expansion: a later write
#	of the same data with the same number of copies uses the entry, and
#	one with more copies is written as a new block instead of adding a
#	copy at the new width to the entry.
#
# STRATEGY:
#	1. Create a raidz2 of 6 and attach 1 disk with -w (7 wide).
#	2. With dedup=on, write a set of files and a second copy of each,
#	   so that each file has a dedup entry born at width 7.
#	3. Attach 2 disks with -w (9 wide).  The labels record 3 width steps
#	   in increasing txgs, the last two at consecutive txgs.
#	4. Write the same data again, once with copies=1 (the dedup ratio
#	   goes up) and once with copies=2.
#	5. Scrub: no checksum errors, nothing repaired.  Verify every file.
#	6. Export.  The copies=1 file shares its first block with the file
#	   written at width 7, the copies=2 file does not.  zdb finds no
#	   errors in any block.
#

verify_runnable "global"

typeset -r dev_size_mb=256
typeset -r devdir=$TEST_BASE_DIR/raidz_expand_multi_009
typeset -r nfiles=16
typeset -A sums

function cleanup
{
	poolexists "$TESTPOOL" && zpool status -v "$TESTPOOL"
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	log_must rm -rf $devdir
}

function record_sums
{
	for f in /$TESTPOOL/fs/*; do
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
	    grep -c "$devdir/dev-")
	if [[ $found -ne $1 ]]; then
		log_fail "expected $1 disks in $TESTPOOL, found $found"
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
# Copy each file a<i> to <prefix><i> with the given number of copies.  dd
# writes the data again, where cp could clone the blocks instead.
#
function copy_set # <prefix> <copies>
{
	log_must zfs set copies=$2 $TESTPOOL/fs
	for i in {1..$nfiles}; do
		log_must dd if=/$TESTPOOL/fs/a$i of=/$TESTPOOL/fs/$1$i bs=128k
	done
	sync_pool $TESTPOOL
}

#
# Print the first DVA of the first L0 block of a file of an exported pool.
#
function first_dva # <object>
{
	zdb -e -p $devdir -ddddddbbbbbb $TESTPOOL/fs $1 | \
	    grep -m 1 'L0 DVA' | sed 's/.*\(DVA\[0\]=<[^>]*>\).*/\1/'
}

log_onexit cleanup

# A directory of its own: zdb -e -p would also find the test suite's
# own file vdevs in $TEST_BASE_DIR, which can carry a pool of the same
# name.
log_must mkdir -p $devdir

log_assert "dedup entries born between two raidz expansions stay readable"

for i in {1..9}; do
	log_must truncate -s ${dev_size_mb}M $devdir/dev-$i
done
d=$devdir/dev-

# 1. raidz2 of 6, plus 1
log_must zpool create -f -o cachefile=none $TESTPOOL \
    raidz2 ${d}1 ${d}2 ${d}3 ${d}4 ${d}5 ${d}6
log_must zfs create -o recordsize=128k $TESTPOOL/filler
log_must fill_fs /$TESTPOOL/filler 1 32 1048576 1 R
log_must zpool attach -w $TESTPOOL raidz2-0 ${d}7
check_width 7
is_pool_scrubbing $TESTPOOL && wait_scrubbed $TESTPOOL

# 2. Dedup entries born at width 7
log_must zfs create -o dedup=on -o compression=off -o recordsize=128k \
    $TESTPOOL/fs
for i in {1..$nfiles}; do
	log_must dd if=/dev/urandom of=/$TESTPOOL/fs/a$i bs=128k count=8
done
sync_pool $TESTPOOL
copy_set b 1
ratio_before=$(get_pool_prop dedupratio $TESTPOOL)
log_note "dedup ratio at width 7: $ratio_before"
record_sums

# 3. plus 2
log_must zpool attach -w $TESTPOOL raidz2-0 ${d}8 ${d}9
check_width 9
set -A steps $(label_steps ${d}1)
log_note "width steps: ${steps[*]}"
if (( ${#steps[@]} != 3 )); then
	log_fail "expected 3 width steps, found ${#steps[@]}"
fi
if (( steps[1] <= steps[0] || steps[2] != steps[1] + 1 )); then
	log_fail "width steps not in order: ${steps[*]}"
fi
is_pool_scrubbing $TESTPOOL && wait_scrubbed $TESTPOOL

# 4. The same data again at width 9
copy_set c 1
ratio_after=$(get_pool_prop dedupratio $TESTPOOL)
log_note "dedup ratio at width 9: $ratio_after"
if ! awk -v a="$ratio_after" -v b="$ratio_before" 'BEGIN { exit !(a > b) }'
then
	log_fail "copies=1 data at width 9 did not use the dedup entries" \
	    "($ratio_before -> $ratio_after)"
fi
copy_set e 2
record_sums
for i in {1..$nfiles}; do
	log_must cmp /$TESTPOOL/fs/a$i /$TESTPOOL/fs/c$i
	log_must cmp /$TESTPOOL/fs/a$i /$TESTPOOL/fs/e$i
done

# 5. Scrub and verify
verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"
verify_sums

# 6. Block pointers and zdb, with the pool exported
obj_a=$(get_objnum /$TESTPOOL/fs/a1)
obj_c=$(get_objnum /$TESTPOOL/fs/c1)
obj_e=$(get_objnum /$TESTPOOL/fs/e1)
log_must_busy zpool export $TESTPOOL
dva_a=$(first_dva $obj_a)
dva_c=$(first_dva $obj_c)
dva_e=$(first_dva $obj_e)
log_note "a1 $dva_a, c1 $dva_c, e1 $dva_e"
[[ $dva_a == DVA* ]] || log_fail "no block pointer found for a1"
[[ $dva_c == "$dva_a" ]] || log_fail "c1 does not share its block with a1"
[[ $dva_e != "$dva_a" ]] || log_fail "e1 shares its block with a1"
log_must zdb -e -p $devdir -bcc $TESTPOOL
log_must zpool import -o cachefile=none -d $devdir $TESTPOOL
check_width 9
verify_sums

log_pass "dedup entries born between two raidz expansions stay readable"
