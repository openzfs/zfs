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
#	Verify that deduplicated writes add no reference to copies on a
#	vdev with allocating=off, so that zfs rewrite moves deduplicated
#	blocks off that vdev.
#
# STRATEGY:
#	1. Create a pool with a raidz vdev and a special mirror, and three
#	   datasets with dedup=on whose data blocks go to the special mirror.
#	2. In each dataset, write a file and a copy of it with dd, so that
#	   every DDT entry has two references.
#	3. Set allocating=off on the special mirror.
#	4. Write a new copy in the third dataset, and verify that it was
#	   written to the raidz vdev, not deduplicated.  Set allocating=on,
#	   verify that another copy is deduplicated again, and set it back
#	   to off.
#	5. Rewrite the first dataset, then the second one with -P, and verify
#	   after each rewrite that the special mirror freed the data, and
#	   that -P kept the logical birth times.
#	6. Verify the data after an export and import, and that zdb finds no
#	   leak.
#	7. Repeat with a legacy dedup table (fast_dedup disabled).

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset VDIR=$TEST_BASE_DIR/zfs_rewrite_dedup_noalloc
typeset -i DATA_SIZE=$((32 * 1024 * 1024))
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

#
# Copy file a to file $1 in the dataset "newdup", and check that the raidz
# vdev grew by at least the size of the data if $2 is "unique", or by
# less than an eighth of it if $2 is "dedup".  The special mirror must
# grow by less than an eighth of it in both cases.
#
function copy_and_check # file unique|dedup
{
	typeset dir
	typeset -i special_before raidz_before special_grew raidz_grew

	dir=$(get_prop mountpoint $TESTPOOL1/newdup)
	special_before=$(special_alloc)
	raidz_before=$(raidz_alloc)
	log_must dd if=$dir/a of=$dir/$1 bs=128k
	sync_pool $TESTPOOL1
	special_grew=$(($(special_alloc) - special_before))
	raidz_grew=$(($(raidz_alloc) - raidz_before))
	log_note "Copy $1 grew $SPECIAL by $special_grew and $RAIDZ by" \
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
	log_must [ "$(xxh128digest $dir/$1)" = "${hash[newdup]}" ]
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
# Rewrite the files of dataset $1 with the options in $2, and wait until
# the special mirror has freed nearly all of their data.
#
function rewrite_and_check # dataset options
{
	typeset fs=$TESTPOOL1/$1
	typeset opts=$2
	typeset -i before=$(special_alloc)
	typeset -i freed=0
	typeset -i i

	log_must zfs rewrite $opts -r $(get_prop mountpoint $fs)
	for i in {1..30}; do
		sync_pool $TESTPOOL1
		freed=$((before - $(special_alloc)))
		((freed >= DATA_SIZE * 7 / 8)) && break
		sleep 1
	done
	log_note "zfs rewrite $opts of $fs freed $freed bytes on $SPECIAL"
	if ((freed < DATA_SIZE * 7 / 8)); then
		zpool list -v $TESTPOOL1
		log_fail "zfs rewrite $opts left the data of $fs on $SPECIAL"
	fi
}

log_assert "Deduplicated writes add no reference to copies on a vdev" \
    "with allocating=off"

log_onexit cleanup

# Flush the dedup log at once, so that freed entries free their blocks.
log_must save_tunable DEDUP_LOG_TXG_MAX
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1

for fast_dedup in enabled disabled; do
	log_must rm -rf $VDIR
	log_must mkdir $VDIR
	log_must truncate -s $MINVDEVSIZE $VDIR/r0 $VDIR/r1 $VDIR/r2 \
	    $VDIR/s0 $VDIR/s1
	log_must zpool create -o feature@fast_dedup=$fast_dedup $TESTPOOL1 \
	    raidz $VDIR/r0 $VDIR/r1 $VDIR/r2 special mirror $VDIR/s0 $VDIR/s1
	SPECIAL=$(zpool status $TESTPOOL1 | \
	    awk '/special/ { found = 1 } found && /mirror-/ { print $1; exit }')
	[[ -n "$SPECIAL" ]] || log_fail "Could not find the special mirror"
	RAIDZ=$(zpool status $TESTPOOL1 | awk '/raidz1-/ { print $1; exit }')
	[[ -n "$RAIDZ" ]] || log_fail "Could not find the raidz vdev"

	for fs in default physical newdup; do
		log_must zfs create -o dedup=on -o recordsize=128k \
		    -o special_small_blocks=128k $TESTPOOL1/$fs
		dir=$(get_prop mountpoint $TESTPOOL1/$fs)
		log_must dd if=/dev/urandom of=$dir/a bs=128k \
		    count=$((DATA_SIZE / (128 * 1024)))
		log_must dd if=$dir/a of=$dir/b bs=128k
		hash[$fs]=$(xxh128digest $dir/a)
		log_must [ "$(xxh128digest $dir/b)" = "${hash[$fs]}" ]
	done
	sync_pool $TESTPOOL1

	# The data of the datasets is on the special mirror, deduplicated.
	alloc=$(special_alloc)
	log_note "$SPECIAL allocated $alloc bytes"
	((alloc >= 3 * DATA_SIZE && alloc < 4 * DATA_SIZE)) || \
	    log_fail "$SPECIAL allocated $alloc bytes, expected about" \
	    "$((3 * DATA_SIZE))"

	log_must zpool set allocating=off $TESTPOOL1 $SPECIAL
	copy_and_check c unique
	log_must zpool set allocating=on $TESTPOOL1 $SPECIAL
	copy_and_check d dedup
	log_must zpool set allocating=off $TESTPOOL1 $SPECIAL

	rewrite_and_check default ""

	births=$(l0_births $TESTPOOL1/physical a)
	log_must [ $(echo "$births" | wc -l) -eq $((DATA_SIZE / (128 * 1024))) ]
	rewrite_and_check physical "-P"
	log_must [ "$(l0_births $TESTPOOL1/physical a)" = "$births" ]

	log_must zpool export $TESTPOOL1
	log_must zpool import -d $VDIR $TESTPOOL1
	for fs in default physical newdup; do
		dir=$(get_prop mountpoint $TESTPOOL1/$fs)
		for f in "$dir"/*; do
			log_must [ "$(xxh128digest $f)" = "${hash[$fs]}" ]
		done
	done
	log_must zdb -b $TESTPOOL1

	destroy_pool $TESTPOOL1
done

log_pass "Deduplicated writes add no reference to copies on a vdev" \
    "with allocating=off"
