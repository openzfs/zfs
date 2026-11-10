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

# DESCRIPTION:
#	Verify that a deduplicated write adds no reference to copies on a
#	vdev with allocating=off whose DDT entry has one reference, so that
#	zfs rewrite moves deduplicated blocks off that vdev.  An entry with
#	more references gets one more.
#
# STRATEGY:
#	1. Create a pool with a raidz vdev and a special mirror, and four
#	   datasets with dedup=on whose data blocks go to the special mirror.
#	2. In the dataset "shared", write a file and a copy of it with dd, so
#	   that every DDT entry has two references.  In the other three, write
#	   a file alone, so that every entry has one reference.
#	3. Set allocating=off on the special mirror.
#	4. Verify that a new copy in "shared" is deduplicated, and that a
#	   rewrite of "shared" leaves its data on the special mirror.
#	5. Write two copies in the dataset "copy", and verify that both were
#	   written to the raidz vdev, not deduplicated.
#	6. Rewrite the other two datasets, one of them with -P, and verify
#	   after each rewrite that the special mirror freed the data, and that
#	   -P kept the logical birth times.
#	7. Verify the data after an export and import, that a scrub finds no
#	   error, and that zdb finds no leak.
#	8. Repeat with a legacy dedup table (fast_dedup disabled).

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset VDIR=$TEST_BASE_DIR/zfs_rewrite_dedup_noalloc
typeset -i DATA_SIZE=$((32 * 1024 * 1024))
typeset -i BLOCKS=$((DATA_SIZE / (128 * 1024)))
typeset -A hash
typeset SPECIAL RAIDZ

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $VDIR
	restore_tunable DEDUP_LOG_TXG_MAX
}

function special_alloc
{
	get_vdev_prop allocated $TESTPOOL1 $SPECIAL
}

function raidz_alloc
{
	get_vdev_prop allocated $TESTPOOL1 $RAIDZ
}

# Create dataset $1 with dedup=on, and write file a there.
function create_and_write # dataset [options]
{
	typeset dir

	log_must zfs create -o dedup=on -o recordsize=128k $2 $TESTPOOL1/$1
	dir=$(get_prop mountpoint $TESTPOOL1/$1)
	log_must dd if=/dev/urandom of=$dir/a bs=128k count=$BLOCKS
	hash[$1]=$(xxh128digest $dir/a)
}

#
# Copy file a to file $2 in dataset $1, and check that the raidz vdev grew
# by at least the size of the data if $3 is "unique", or by less than an
# eighth of it if $3 is "dedup".  The special mirror must grow by less
# than an eighth of it in both cases.
#
function copy_and_check # dataset file unique|dedup
{
	typeset fs=$1
	typeset dir
	typeset -i special_before raidz_before special_grew raidz_grew

	shift
	dir=$(get_prop mountpoint $TESTPOOL1/$fs)
	special_before=$(special_alloc)
	raidz_before=$(raidz_alloc)
	log_must dd if=$dir/a of=$dir/$1 bs=128k
	sync_pool $TESTPOOL1
	special_grew=$(($(special_alloc) - special_before))
	raidz_grew=$(($(raidz_alloc) - raidz_before))
	log_note "Copy $fs/$1 grew $SPECIAL by $special_grew and $RAIDZ by" \
	    "$raidz_grew bytes"
	((special_grew < DATA_SIZE / 8)) || \
	    log_fail "Copy $1 was written to $SPECIAL"
	if [[ $2 == unique ]]; then
		((raidz_grew >= DATA_SIZE)) || \
		    log_fail "Copy $1 was deduplicated"
	else
		((raidz_grew < DATA_SIZE / 8)) || \
		    log_fail "Copy $1 was not deduplicated"
	fi
	log_must [ "$(xxh128digest $dir/$1)" = "${hash[$fs]}" ]
}

# Print the logical birth time of each L0 block of file $2 in dataset $1.
function l0_births # dataset file
{
	zdb -Ovv $1 $2 | awk '/ L0 / {
		for (i = 4; i <= NF; i++)
			if (sub(/^B=/, "", $i))
				break
		sub(/\/.*/, "", $i)
		print $i
	}'
}

#
# Rewrite the files of dataset $1 with the options in $2.  If $3 is "moved",
# wait until the special mirror has freed nearly all of their data.  If it
# is "kept", check that the data stayed there, deduplicated.
#
function rewrite_and_check # dataset options moved|kept
{
	typeset fs=$TESTPOOL1/$1
	typeset opts=$2
	typeset -i before=$(special_alloc)
	typeset -i raidz_before=$(raidz_alloc)
	typeset -i freed=0 raidz_grew
	typeset -i i

	log_must zfs rewrite $opts -r $(get_prop mountpoint $fs)
	for i in {1..30}; do
		sync_pool $TESTPOOL1
		freed=$((before - $(special_alloc)))
		[[ $3 == kept ]] && break
		((freed >= DATA_SIZE * 7 / 8)) && break
		sleep 1
	done
	raidz_grew=$(($(raidz_alloc) - raidz_before))
	log_note "zfs rewrite $opts of $fs freed $freed bytes on $SPECIAL," \
	    "and grew $RAIDZ by $raidz_grew bytes"
	if [[ $3 == kept ]]; then
		((freed < DATA_SIZE / 8 && raidz_grew < DATA_SIZE / 8)) || \
		    log_fail "zfs rewrite $opts moved the data of $fs"
	elif ((freed < DATA_SIZE * 7 / 8)); then
		zpool list -v $TESTPOOL1
		log_fail "zfs rewrite $opts left the data of $fs on $SPECIAL"
	fi
}

log_assert "Deduplicated writes add no reference to copies on a vdev" \
    "with allocating=off whose entry has one reference"

log_onexit cleanup

# Flush the dedup log at once, so that freed entries free their blocks.
log_must save_tunable DEDUP_LOG_TXG_MAX
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1

for fast_dedup in enabled disabled; do
	log_must rm -rf $VDIR
	log_must mkdir $VDIR
	log_must truncate -s $MINVDEVSIZE $VDIR/r0 $VDIR/r1 $VDIR/r2
	# The four datasets take 4 * DATA_SIZE of the special mirror, and data
	# only goes there while it is less than 75% full.
	log_must truncate -s $((2 * MINVDEVSIZE)) $VDIR/s0 $VDIR/s1
	log_must zpool create -o feature@fast_dedup=$fast_dedup $TESTPOOL1 \
	    raidz $VDIR/r0 $VDIR/r1 $VDIR/r2 special mirror $VDIR/s0 $VDIR/s1
	SPECIAL=$(zpool status $TESTPOOL1 | \
	    awk '/special/ { found = 1 } found && /mirror-/ { print $1; exit }')
	[[ -n "$SPECIAL" ]] || log_fail "Could not find the special mirror"
	RAIDZ=$(zpool status $TESTPOOL1 | awk '/raidz1-/ { print $1; exit }')
	[[ -n "$RAIDZ" ]] || log_fail "Could not find the raidz vdev"

	for fs in shared copy single singlep; do
		create_and_write $fs "-o special_small_blocks=128k"
	done
	dir=$(get_prop mountpoint $TESTPOOL1/shared)
	log_must dd if=$dir/a of=$dir/b bs=128k
	sync_pool $TESTPOOL1

	# The data of the datasets is on the special mirror, deduplicated.
	alloc=$(special_alloc)
	log_note "$SPECIAL allocated $alloc bytes"
	((alloc >= 4 * DATA_SIZE && alloc < 5 * DATA_SIZE)) || \
	    log_fail "$SPECIAL allocated $alloc bytes, expected about" \
	    "$((4 * DATA_SIZE))"

	log_must zpool set allocating=off $TESTPOOL1 $SPECIAL

	# An entry with two references gets one more.
	copy_and_check shared c dedup
	rewrite_and_check shared "" kept

	# Entries with one reference are not deduplicated.
	copy_and_check copy c unique
	copy_and_check copy d unique
	log_must rm $(get_prop mountpoint $TESTPOOL1/copy)/a

	rewrite_and_check single "" moved
	births=$(l0_births $TESTPOOL1/singlep a)
	log_must [ $(echo "$births" | wc -l) -eq $BLOCKS ]
	rewrite_and_check singlep "-P" moved
	log_must [ "$(l0_births $TESTPOOL1/singlep a)" = "$births" ]

	log_must zpool export $TESTPOOL1
	log_must zpool import -d $VDIR $TESTPOOL1
	for fs in shared copy single singlep; do
		dir=$(get_prop mountpoint $TESTPOOL1/$fs)
		for f in "$dir"/*; do
			log_must [ "$(xxh128digest $f)" = "${hash[$fs]}" ]
		done
	done
	log_must zpool scrub -w $TESTPOOL1
	log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"
	log_must zdb -b $TESTPOOL1
	destroy_pool $TESTPOOL1
done

log_pass "Deduplicated writes add no reference to copies on a vdev" \
    "with allocating=off whose entry has one reference"
