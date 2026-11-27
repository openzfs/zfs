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
#	During a raidz expansion that adds several disks at once, a block
#	that straddles the reflow offset has rows at their new location and
#	rows still at their old one, which is as many disks wide as the vdev
#	was before the expansion, not one disk narrower than it is now.
#	Damage from as many failed disks as there is parity must remain
#	repairable in such a block, also when the reflow has copied some of
#	the damage to the new location.  This is the several-disk counterpart
#	of raidz_expand_boundary_repair.
#
# STRATEGY:
#	For each case (parity, old width, disks added, damaged old disks):
#	1. Create a raidz pool of that parity and width with large
#	   metaslabs, so that the file is written in one piece, write a file
#	   of 1M blocks and attach the new disks in one expansion with the
#	   reflow paused early on.
#	2. Export the pool and zero the part of the damaged old disks that
#	   holds the data the reflow has not copied yet.
#	3. Import the pool and let the reflow copy some more of the data, in
#	   chunks that are not a multiple of the block size so that it can
#	   stop inside a block.  It copies the damage along, spread over all
#	   disks.
#	4. Verify that a block of the file straddles the new reflow offset,
#	   with at least three rows on either side, its first part copied
#	   from the damaged area and its columns on the zeroed disks holding
#	   data (if none does, let the reflow copy a little more and look
#	   again, until one does or the reflow is done; then start the case
#	   again on a new pool, up to five times).  Every row of that block
#	   has a damaged sector per zeroed disk, on that disk in the rows at
#	   the old location and wherever the reflow put it in the others.
#	5. Scrub with the reflow paused.  Verify that it found no data
#	   errors, repaired something and that the file is intact.
#	6. Let the expansion finish, scrub again and verify that it repairs
#	   nothing.
#

verify_runnable "global"

typeset -r dev_size_mb=512
# Rows of the straddling block required on either side of the offset.
typeset -r minrows=3
# A directory of our own, so that zdb -e and zpool import only see this pool.
typeset -r devdir=$TEST_BASE_DIR/raidz_expand_multi_011_pos

prefetch_disable=$(get_tunable PREFETCH_DISABLE)
scrub_after_expand=$(get_tunable SCRUB_AFTER_EXPAND)
max_copy_bytes=$(get_tunable RAIDZ_EXPAND_MAX_COPY_BYTES)
min_ms_count=$(get_tunable VDEV_MIN_MS_COUNT)
txg_timeout=$(get_tunable TXG_TIMEOUT)

function cleanup
{
	poolexists "$TESTPOOL" && zpool status -v "$TESTPOOL"
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	log_must rm -rf "$devdir"

	log_must set_tunable32 PREFETCH_DISABLE $prefetch_disable
	log_must set_tunable32 SCRUB_AFTER_EXPAND $scrub_after_expand
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
	log_must set_tunable64 RAIDZ_EXPAND_MAX_COPY_BYTES $max_copy_bytes
	log_must set_tunable32 VDEV_MIN_MS_COUNT $min_ms_count
	log_must set_tunable32 TXG_TIMEOUT $txg_timeout
}

function reflowed # pool
{
	zpool status -jp $1 | jq -r '.pools[].raidz_expand_stats.reflowed // 0'
}

#
# The reflow offset (in the raidz vdev's address space) of the exported pool.
#
function reflow_offset
{
	zdb -e -p $devdir -u $TESTPOOL | \
	    awk '/raidz_reflow/ { sub("off=", "", $3); print $3 }'
}

#
# Print the offset and allocated size of each level 0 block of the file in
# the exported pool, in decimal.
#
function file_blocks # object
{
	zdb -e -p $devdir -ddddd $TESTPOOL/fs $1 | \
	    awk '$2 == "L0" { print $3 }' | while IFS=: read _ off asize; do
		echo $((16#$off)) $((16#$asize))
	done
}

#
# Is the column of the block at vdev offset $1 on old disk $2 a parity
# column?  Columns start at disk (offset / sector) % old_width; raidz1
# swaps the first two columns in every other MiB.
#
function parity_column # off disk
{
	typeset -i first=$(($1 / 4096))
	typeset -i j

	if (( parity == 1 && ($1 >> 20) & 1 )); then
		(( ($2 - first - 1) % old_width == 0 ))
		return
	fi
	for (( j = 0; j < parity; j++ )); do
		(( (first + j) % old_width == $2 )) && return 0
	done
	return 1
}

#
# Print the old disks that hold the data columns of the block at vdev
# offset $1, in column order.
#
function data_disks # off
{
	typeset -i first=$(($1 / 4096))
	typeset -i j d

	for (( j = 0; j < old_width; j++ )); do
		d=$(((first + j) % old_width))
		parity_column $1 $d || echo $d
	done
}

#
# Damage $4 old disks of a raidz$1 vdev $2 disks wide before an expansion
# by $3 disks, scrub with a block straddling the reflow offset and verify
# the damage is repaired.  Return 1 if the reflow never stopped inside a
# suitable block.  The damaged disks are data disks $5 to
# $5 + $4 - 1 (in column order) of the first block of the file that the
# reflow has not copied yet.  Blocks of 1M are a multiple of the width
# for most layouts, so they all start on the same disk.
#
function test_case # parity old_width added nbad first
{
	# Globals, for parity_column().
	parity=$1
	old_width=$2
	added=$3
	typeset -i nbad=$4 skip=$5
	typeset bads
	typeset -a disks newdisks
	typeset -i bad

	log_note "raidz$parity, $old_width -> $((old_width + added)) wide," \
	    "damaging $nbad old disks"
	log_must mkdir -p $devdir
	for i in {0..$((old_width + added - 1))}; do
		log_must truncate -s ${dev_size_mb}M $devdir/dev-$i
		if (( i < old_width )); then
			disks[${#disks[*]}+1]=$devdir/dev-$i
		else
			newdisks[${#newdisks[*]}+1]=$devdir/dev-$i
		fi
	done

	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
	log_must set_tunable64 RAIDZ_EXPAND_MAX_COPY_BYTES $max_copy_bytes
	log_must set_tunable32 TXG_TIMEOUT $txg_timeout
	log_must zpool create -f -o cachefile=none -o ashift=12 $TESTPOOL \
	    raidz$parity ${disks[@]}
	log_must zfs create -o recordsize=1M -o compression=off \
	    -o primarycache=metadata $TESTPOOL/fs
	log_must dd if=/dev/urandom of=/$TESTPOOL/fs/file bs=1M count=300
	typeset digest=$(xxh128digest /$TESTPOOL/fs/file)
	typeset object=$(ls -i /$TESTPOOL/fs/file | awk '{print $1}')

	log_must zpool sync $TESTPOOL
	typeset allocated=$(get_pool_prop allocated $TESTPOOL)
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $((allocated / 80))
	log_must zpool attach $TESTPOOL raidz$parity-0 ${newdisks[@]}
	wait_raidz_expand_paused $TESTPOOL
	log_must eval "zpool status $TESTPOOL | \
	    grep -q 'expansion of .* in progress'"
	log_must_busy zpool export $TESTPOOL

	#
	# Rows past the reflow offset are still at their old location, row r
	# on each old disk at byte offset 4M (the labels and boot area) +
	# r * 4k, $old_width disks wide.  Zero the bad disks from the first
	# such row on.  The copied part of the pool is in the rows before
	# that (they are wider) and stays intact.
	#
	typeset offset1=$(reflow_offset)
	log_note "reflow offset $offset1 after the first pause"
	(( offset1 > 0 )) || log_fail "no reflow offset in the uberblock"
	typeset row=$((offset1 / 4096 / old_width))
	typeset ref=$(file_blocks $object | \
	    awk -v o=$offset1 '$1 >= o { print $1; exit }')
	[[ -n $ref ]] || log_fail "no block of the file past $offset1"
	bads=$(data_disks $ref | awk -v s=$skip -v n=$nbad \
	    'NR > s && NR <= s + n')
	bads=$(echo $bads)
	(( $(echo $bads | wc -w) == nbad )) || \
	    log_fail "block at $ref has too few data disks"
	log_note "damaging old disks $bads (block at $ref)"
	for bad in $bads; do
		log_must dd conv=notrunc if=/dev/zero of=$devdir/dev-$bad \
		    bs=4k seek=$((1024 + row)) \
		    count=$((dev_size_mb * 256 - 1024 - row - 256))
	done

	log_must zpool import -o cachefile=none -d $devdir $TESTPOOL
	log_must set_tunable64 RAIDZ_EXPAND_MAX_COPY_BYTES $((1024 * 1024 + 4096))

	#
	# Look for a block that straddles the offset, with at least $minrows
	# rows on either side.  The block was written $old_width wide, so a
	# row of it is $old_width sectors of the vdev's address space, on
	# every old disk at the old location.  Each column of the block is on
	# the same old disk in every row; the columns on the bad disks must
	# not be parity, or the scrub finds the data intact and only rewrites
	# the parity.  If no block qualifies, let the reflow copy a little
	# more and look again.
	#
	# The amount copied only advances when a txg syncs, so the reflow
	# pauses at the first txg boundary after the pause point.  With the
	# default txg timeout it copies up to the most it may per txg, and
	# stops in the file's data only once or twice.  Short txgs make it
	# stop more often, though not always inside a block that qualifies.
	#
	# In the rows at the old location, the damaged column is on
	# consecutive sectors of the bad disk, so with three or more of them
	# at least two are not a multiple of the old width.  For those,
	# numbering the old rows by the current width minus one rather than
	# by the old width points a simulated failure of the bad disk at
	# another column, unless the current width minus one is a multiple
	# of the old width.
	#
	typeset -i straddling=0
	typeset rowsize=$((old_width * 4096))
	typeset offset2
	log_must set_tunable32 TXG_TIMEOUT 1
	while (( straddling == 0 )); do
		log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES \
		    $(($(reflowed $TESTPOOL) + 1))
		wait_raidz_expand_paused $TESTPOOL
		log_must_busy zpool export $TESTPOOL

		offset2=$(reflow_offset)
		log_note "reflow offset $offset2 after the second pause"
		if (( offset2 == 0 )); then
			log_must zpool import -o cachefile=none -d $devdir \
			    $TESTPOOL
			break
		fi
		file_blocks $object | while read off asize; do
			(( off >= offset1 &&
			    off + minrows * rowsize <= offset2 &&
			    offset2 + minrows * rowsize <= off + asize )) || \
			    continue
			for bad in $bads; do
				parity_column $off $bad && continue 2
			done
			log_note "block at $off (asize $asize) straddles it"
			(( straddling += 1 ))
		done
		log_must zpool import -o cachefile=none -d $devdir $TESTPOOL
	done
	log_must set_tunable32 TXG_TIMEOUT $txg_timeout
	if (( straddling == 0 )); then
		log_note "no block of the file straddles the reflow offset"
		log_must_busy zpool destroy $TESTPOOL
		log_must rm -rf $devdir
		return 1
	fi

	typeset before=$(reflowed $TESTPOOL)
	log_must zpool scrub -w $TESTPOOL
	log_note "reflowed $before before the scrub," \
	    "$(reflowed $TESTPOOL) after it"
	log_must eval "zpool status -v $TESTPOOL"
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	log_must eval "zpool status $TESTPOOL | \
	    grep -q 'expansion of .* in progress'"
	log_mustnot check_pool_status $TESTPOOL "scan" "repaired 0B"
	[[ $(xxh128digest /$TESTPOOL/fs/file) == $digest ]] || \
	    log_fail "the file changed"
	log_must zpool clear $TESTPOOL

	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
	log_must zpool wait -t raidz_expand $TESTPOOL
	log_must zpool scrub -w $TESTPOOL
	log_must eval "zpool status -v $TESTPOOL"
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
	[[ $(xxh128digest /$TESTPOOL/fs/file) == $digest ]] || \
	    log_fail "the file changed"

	log_must_busy zpool destroy $TESTPOOL
	log_must rm -rf $devdir
	return 0
}

log_onexit cleanup

log_must set_tunable32 PREFETCH_DISABLE 1
log_must set_tunable32 SCRUB_AFTER_EXPAND 0
#
# With the default 16 metaslabs, the file ends up in several pieces with
# free space between them, and the reflow moves through free space in big
# steps.  With 2 metaslabs the file is in one piece in the first metaslab.
#
log_must set_tunable32 VDEV_MIN_MS_COUNT 2

# parity old_width added nbad first
set -A cases \
    "1 3 3 1 0" \
    "1 3 3 1 1" \
    "2 4 3 2 0" \
    "3 5 3 1 0"
#
# The reflow sometimes moves on in big steps and never stops inside a
# block that qualifies; start the case again on a new pool then.
#
typeset -i tries
for c in "${cases[@]}"; do
	tries=0
	until test_case $c; do
		(( ++tries < 5 )) || log_unsupported "case $c: no block of" \
		    "the file straddles the reflow offset in $tries tries"
	done
done

log_pass "Damage in a block across the reflow offset of a raidz" \
    "expansion by several disks is repairable."
