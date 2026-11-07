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
#	blocks off that vdev, and that a block whose entry has no other
#	reference stays deduplicated, unless the dedup table is a legacy one.
#
# STRATEGY:
#	1. Create a pool with a raidz vdev and a special mirror, and six
#	   datasets with dedup=on whose data blocks go to the special mirror.
#	2. In three datasets, write a file and a copy of it with dd, so that
#	   every DDT entry has two references.  In the other three, write a
#	   file alone, so that every entry has one reference, and snapshot
#	   one of them.
#	3. Set allocating=off on the special mirror.
#	4. Write a new copy in the third dataset, and verify that it was
#	   written to the raidz vdev, not deduplicated.  Set allocating=on,
#	   verify that another copy is deduplicated again, and set it back
#	   to off.
#	5. Rewrite the first dataset, then the second one with -P, and verify
#	   after each rewrite that the special mirror freed the data, and
#	   that -P kept the logical birth times.
#	6. Rewrite the three datasets whose entries have one reference, one
#	   of them with -P, and verify that a copy of their file is
#	   deduplicated, so that their entries now describe the new blocks,
#	   or with a legacy dedup table that it is not, and that -P kept the
#	   logical birth times.  Corrupt some of the old blocks that the snapshot
#	   keeps on one side of the special mirror, and verify that a scrub
#	   repairs them, that zdb finds no leak, and that destroying the
#	   snapshot frees them from the special mirror.
#	7. Verify the data after an export and import, that a scrub finds no
#	   error, and that zdb finds no leak.
#	8. Repeat with a legacy dedup table (fast_dedup disabled).

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset VDIR=$TEST_BASE_DIR/zfs_rewrite_dedup_noalloc
typeset -i DATA_SIZE=$((32 * 1024 * 1024))
typeset -A hash
typeset SPECIAL RAIDZ
# Offset of the allocatable space of a vdev, after the front labels
typeset -i LABEL_START=$((4 * 1024 * 1024))

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
# Print "offset asize" in decimal for each L0 block of file $2 in dataset
# $1 that is on the special mirror.
#
function special_l0_dvas # dataset file
{
	zdb -Ovv $1 $2 | awk -v id=${SPECIAL##*-} '
	    $2 == "L0" && $3 ~ "^" id ":[0-9a-f]+:[0-9a-f]+$" {
		split($3, f, ":")
		print f[2], f[3]
	    }' | while read -r off asize; do
		echo $((16#$off)) $((16#$asize))
	done
}

# Whether both sides of the special mirror hold the same bytes at the
# blocks listed in file $1.
function special_sides_match # file
{
	typeset off asize skip

	while read -r off asize; do
		skip=$(((off + LABEL_START) / 512))
		cmp -s \
		    <(dd if=$VDIR/s0 bs=512 skip=$skip count=$((asize / 512)) \
		    2>/dev/null) \
		    <(dd if=$VDIR/s1 bs=512 skip=$skip count=$((asize / 512)) \
		    2>/dev/null) || return 1
	done < $1
	return 0
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
    "with allocating=off, and keep an entry with one reference"

log_onexit cleanup

# Flush the dedup log at once, so that freed entries free their blocks.
log_must save_tunable DEDUP_LOG_TXG_MAX
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1

for fast_dedup in enabled disabled; do
	log_must rm -rf $VDIR
	log_must mkdir $VDIR
	log_must truncate -s $MINVDEVSIZE $VDIR/r0 $VDIR/r1 $VDIR/r2
	# The six datasets take 6 * DATA_SIZE of the special mirror, and data
	# only goes there while it is less than 75% full.
	log_must truncate -s $((2 * MINVDEVSIZE)) $VDIR/s0 $VDIR/s1
	log_must zpool create -o feature@fast_dedup=$fast_dedup $TESTPOOL1 \
	    raidz $VDIR/r0 $VDIR/r1 $VDIR/r2 special mirror $VDIR/s0 $VDIR/s1
	SPECIAL=$(zpool status $TESTPOOL1 | \
	    awk '/special/ { found = 1 } found && /mirror-/ { print $1; exit }')
	[[ -n "$SPECIAL" ]] || log_fail "Could not find the special mirror"
	RAIDZ=$(zpool status $TESTPOOL1 | awk '/raidz1-/ { print $1; exit }')
	[[ -n "$RAIDZ" ]] || log_fail "Could not find the raidz vdev"

	for fs in default physical newdup single singlep snap; do
		log_must zfs create -o dedup=on -o recordsize=128k \
		    -o special_small_blocks=128k $TESTPOOL1/$fs
		dir=$(get_prop mountpoint $TESTPOOL1/$fs)
		log_must dd if=/dev/urandom of=$dir/a bs=128k \
		    count=$((DATA_SIZE / (128 * 1024)))
		hash[$fs]=$(xxh128digest $dir/a)
		[[ $fs == single || $fs == singlep || $fs == snap ]] && continue
		log_must dd if=$dir/a of=$dir/b bs=128k
		log_must [ "$(xxh128digest $dir/b)" = "${hash[$fs]}" ]
	done
	log_must zfs snapshot $TESTPOOL1/snap@old
	sync_pool $TESTPOOL1

	# The data of the datasets is on the special mirror, deduplicated.
	alloc=$(special_alloc)
	log_note "$SPECIAL allocated $alloc bytes"
	((alloc >= 6 * DATA_SIZE && alloc < 7 * DATA_SIZE)) || \
	    log_fail "$SPECIAL allocated $alloc bytes, expected about" \
	    "$((6 * DATA_SIZE))"

	log_must zpool set allocating=off $TESTPOOL1 $SPECIAL
	copy_and_check newdup c unique
	log_must zpool set allocating=on $TESTPOOL1 $SPECIAL
	copy_and_check newdup d dedup
	log_must zpool set allocating=off $TESTPOOL1 $SPECIAL

	rewrite_and_check default ""

	births=$(l0_births $TESTPOOL1/physical a)
	log_must [ $(echo "$births" | wc -l) -eq $((DATA_SIZE / (128 * 1024))) ]
	rewrite_and_check physical "-P"
	log_must [ "$(l0_births $TESTPOOL1/physical a)" = "$births" ]

	# Entries with one reference take the new copies, if not legacy.
	[[ $fast_dedup == enabled ]] && moved=dedup || moved=unique
	rewrite_and_check single ""
	copy_and_check single c $moved
	births=$(l0_births $TESTPOOL1/singlep a)
	rewrite_and_check singlep "-P"
	log_must [ "$(l0_births $TESTPOOL1/singlep a)" = "$births" ]
	copy_and_check singlep c $moved
	log_must eval "special_l0_dvas $TESTPOOL1/snap a > $VDIR/dvas"
	log_must [ "$(wc -l < $VDIR/dvas)" -eq $((DATA_SIZE / (128 * 1024))) ]
	log_must eval "head -8 $VDIR/dvas > $VDIR/corrupt"
	log_must zfs rewrite -r "$(get_prop mountpoint $TESTPOOL1/snap)"
	sync_pool $TESTPOOL1
	copy_and_check snap c $moved

	#
	# The entries of the old blocks that the snapshot keeps may now
	# describe other copies.  A scrub must still read the old blocks.
	#
	log_must zpool export $TESTPOOL1
	while read -r off asize; do
		log_must dd if=/dev/urandom of=$VDIR/s1 bs=512 \
		    seek=$(((off + LABEL_START) / 512)) \
		    count=$((asize / 512)) conv=notrunc
	done < $VDIR/corrupt
	log_mustnot special_sides_match $VDIR/corrupt
	log_must zpool import -d $VDIR $TESTPOOL1
	log_must zpool scrub -w $TESTPOOL1
	log_must zpool export $TESTPOOL1
	log_must special_sides_match $VDIR/corrupt
	log_must zpool import -d $VDIR $TESTPOOL1
	log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"
	log_must zdb -b $TESTPOOL1
	before=$(special_alloc)
	log_must zfs destroy $TESTPOOL1/snap@old
	for i in {1..30}; do
		sync_pool $TESTPOOL1
		freed=$((before - $(special_alloc)))
		((freed >= DATA_SIZE * 7 / 8)) && break
		sleep 1
	done
	log_note "Destroying snap@old freed $freed bytes on $SPECIAL"
	((freed >= DATA_SIZE * 7 / 8)) || \
	    log_fail "Destroying snap@old left its blocks on $SPECIAL"

	log_must zpool export $TESTPOOL1
	log_must zpool import -d $VDIR $TESTPOOL1
	for fs in default physical newdup single singlep snap; do
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
    "with allocating=off, and keep an entry with one reference"
