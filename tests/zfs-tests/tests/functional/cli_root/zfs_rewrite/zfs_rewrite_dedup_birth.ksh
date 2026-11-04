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
#	Verify zfs rewrite -P keeps the logical birth time of the
#	deduplicated blocks it writes anew.
#
# STRATEGY:
#	1. On the test pool (fast dedup), write a file with checksum=sha256
#	   and dedup off, snapshot it and turn dedup on.
#	2. On a pool without fast dedup, write a file with dedup on and
#	   copies=2, snapshot it and set copies=1.
#	3. In both cases, rewrite the file with -P and verify that every
#	   block moved into the DDT, kept its logical birth time, and is left
#	   out of an incremental send from the snapshot.
#	4. Rewrite it again with -P and verify that nothing changed, then
#	   remove it and verify that its DDT entries are gone once the
#	   snapshots are destroyed.

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset legacy=$TESTPOOL-legacy
typeset vdev=$TEST_BASE_DIR/zfs_rewrite_dedup_birth.vdev
typeset tmp=$(mktemp)
typeset before=$(mktemp)
typeset after=$(mktemp)
typeset stream=$(mktemp)

function cleanup
{
	poolexists $legacy && destroy_pool $legacy
	datasetexists $TESTPOOL/$TESTFS/dedup && \
	    destroy_dataset $TESTPOOL/$TESTFS/dedup -r
	rm -f $vdev $tmp $before $after $stream
}

# Save the DVA and logical/physical birth times of each L0 block of $1.
function save_blocks
{
	log_must eval "zdb -Ovv $1 file > $tmp"
	awk '/ L0 / {
		for (i = 4; i <= NF; i++)
			if (sub(/^B=/, "", $i))
				break
		split($i, b, "/")
		print $3, b[1], b[2]
	}' < $tmp > $2
	log_must [ $(awk 'END { print NR }' < $2) -eq 4 ]
}

# Verify that $1 blocks moved and $2 have a new logical birth time.
function verify_blocks
{
	typeset counts=$(awk '
	    NR == FNR { dva[FNR] = $1; birth[FNR] = $2; next }
	    { moved += ($1 != dva[FNR]); born += ($2 != birth[FNR]) }
	    END { print moved + 0, born + 0 }' $before $after)

	if [[ "$counts" != "$1 $2" ]]; then
		log_note "Before: $(cat $before)"
		log_note "After: $(cat $after)"
		log_fail "Moved and newly born blocks: $counts, expected: $1 $2"
	fi
	log_note "Moved and newly born blocks: $counts"
	log_must mv $after $before
}

function ddt_entries
{
	zpool status -D $1 | awk '/dedup: DDT entries/ { n = $4 }
	    END { print n + 0 }'
}

# Rewrite file in dataset $1 after setting property $2 (dedup or copies).
function test_rewrite
{
	typeset pool=${1%%/*}
	typeset file=$(get_prop mountpoint $1)/file
	typeset entries=$(ddt_entries $pool)
	typeset hash i

	log_must dd if=/dev/urandom of=$file bs=128k count=4
	log_must sync_pool $pool
	hash=$(xxh128digest $file)
	save_blocks $1 $before
	log_must zfs snapshot $1@snap
	log_must zfs set $2 $1

	log_must zfs rewrite -P $file
	log_must sync_pool $pool
	save_blocks $1 $after
	verify_blocks 4 0
	log_must [ "$(ddt_entries $pool)" -eq $((entries + 4)) ]

	log_must zfs snapshot $1@snap2
	log_must eval "zfs send -i @snap $1@snap2 > $stream"
	typeset size=$(wc -c < $stream)
	log_note "Incremental send after rewrite -P: $size bytes"
	log_must [ $size -lt $((128 * 1024)) ]

	log_must zfs rewrite -P $file
	log_must sync_pool $pool
	save_blocks $1 $after
	verify_blocks 0 0
	log_must [ "$(xxh128digest $file)" = "$hash" ]

	log_must rm $file
	log_must zfs destroy $1@snap2
	log_must zfs destroy $1@snap
	for i in {1..10}; do
		log_must sync_pool $pool
		(( $(ddt_entries $pool) == entries )) && return
	done
	log_must zpool status -D $pool
	log_fail "$pool: $(ddt_entries $pool) DDT entries, expected $entries"
}

log_assert "zfs rewrite -P keeps the logical birth of deduplicated blocks"

log_onexit cleanup

log_must zfs create -o recordsize=128k -o compression=off \
    -o checksum=sha256 $TESTPOOL/$TESTFS/dedup
test_rewrite $TESTPOOL/$TESTFS/dedup dedup=sha256

log_must truncate -s $MINVDEVSIZE $vdev
log_must zpool create -f -o feature@fast_dedup=disabled \
    -O recordsize=128k -O compression=off -O dedup=sha256 -O copies=2 \
    $legacy $vdev
log_must zfs create $legacy/fs
test_rewrite $legacy/fs copies=1

log_pass "zfs rewrite -P keeps the logical birth of deduplicated blocks"
