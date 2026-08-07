#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/redacted_send/redacted.kshlib

#
# DESCRIPTION:
#	A redacted send must not confuse a meta-dnode range with the ranges of
#	the objects it covers.  The two count blocks in different units, and
#	the send merge used to treat them as starting at the same place.
#
# STRATEGY:
#	A meta-dnode range covering dnode block b is positioned, for sorting,
#	at object b * DNODES_PER_BLOCK.  That is the same position as the
#	first block of that object, so a redaction entry for exactly that
#	object ties with the meta-dnode range covering it.  Build that tie:
#	free a whole aligned dnode block in the sending snapshot while a
#	redaction list holds an entry for the first object in it.
#
#	1. An incremental send carrying the redaction list on --redact.
#	2. An incremental send from the redaction bookmark itself, which is
#	   the documented workflow and resolves the tie the other way round.
#
#	The construction is asserted rather than assumed: if no aligned dnode
#	block can be filled and freed, the test fails instead of passing
#	without having built the case it exists to cover.
#

verify_runnable "both"

# Slots per dnode block.  The search below wants an aligned run of this many
# consecutive object numbers, which holds only while a dnode occupies a single
# slot, so the sending dataset is created with dnodesize=legacy rather than
# inheriting a larger one from the pool.
typeset -ri DNODES_PER_BLOCK=32
typeset -ri NFILES=800

typeset sendfs="$POOL/$FS"
typeset clone="$POOL/${FS}_clone"
typeset stream=$(mktemp -t meta_dnode.XXXX)
typeset inodes=$(mktemp -t meta_dnode_inodes.XXXX)

function cleanup
{
	redacted_cleanup $sendfs $clone
	rm -f $stream $inodes
}

log_onexit cleanup

log_assert "a meta-dnode hole and a redaction entry for the object it covers"

log_must zfs create -o dnodesize=legacy $sendfs
typeset mntpnt=$(get_prop mountpoint $sendfs)

#
# Enough small files that some aligned dnode block is filled entirely by
# them.  Object numbers are allocated in chunks, so this is not guaranteed
# for any particular block; the search below finds one that worked.
#
typeset -i i=0
while (( i < NFILES )); do
	echo "$i" >$mntpnt/f$i
	(( i = i + 1 ))
done
log_must sync_pool $POOL

#
# Find an aligned block whose every slot belongs to one of our files.  A
# file's inode number is its object number.
#
log_must eval "ls -i $mntpnt >$inodes"
typeset -i blk=$(awk -v n=$DNODES_PER_BLOCK '
	{ seen[$1] = 1 }
	END {
		for (k = 1; k < 100000; k++) {
			ok = 1
			for (j = 0; j < n; j++)
				if (!((k * n + j) in seen)) { ok = 0; break }
			if (ok) { print k; exit }
		}
		print 0
	}' $inodes)

if (( blk == 0 )); then
	log_fail "no aligned dnode block was filled by these files, so the " \
	    "meta-dnode tie this test exists to build was never constructed"
fi

typeset -ri boundary_obj=$(( blk * DNODES_PER_BLOCK ))
typeset -ri last_obj=$(( boundary_obj + DNODES_PER_BLOCK - 1 ))
log_note "using dnode block $blk, objects $boundary_obj..$last_obj"

typeset boundary_file=$(awk -v b=$boundary_obj '$1 == b { print $2 }' \
    $inodes)
typeset block_files=$(awk -v lo=$boundary_obj -v hi=$last_obj \
    '$1 >= lo && $1 <= hi { print $2 }' $inodes)

if [[ -z $boundary_file ]]; then
	log_fail "no file owns object $boundary_obj"
fi
if (( $(echo "$block_files" | wc -l) != DNODES_PER_BLOCK )); then
	log_fail "expected $DNODES_PER_BLOCK files in the block, found " \
	    "$(echo "$block_files" | wc -l)"
fi

log_must zfs snapshot $sendfs@snap1

#
# The redaction list gets an entry for the boundary object, and only that
# object, by deleting just that file in the clone.
#
log_must zfs clone $sendfs@snap1 $clone
typeset clone_mnt=$(get_prop mountpoint $clone)
log_must rm -f $clone_mnt/$boundary_file
log_must zfs snapshot $clone@red
log_must zfs redact $sendfs@snap1 book $clone@red

log_must zfs snapshot $sendfs@snap2

#
# Free the whole dnode block after the incremental source, so the sending
# snapshot has a meta-dnode hole covering exactly the objects the redaction
# list refers to.
#
typeset f
for f in $block_files; do
	log_must rm -f $mntpnt/$f
done
log_must sync_pool $POOL
log_must zfs snapshot $sendfs@snap3

# 1. the redaction list arrives on the REDACT queue
log_must eval "zfs send --redact book -i $sendfs@snap2 $sendfs@snap3 >$stream"
log_must test -s $stream

# 2. the redaction list arrives on the FROM queue instead, which orders the
#    tie the other way and is the documented incremental workflow
log_must eval "zfs send -i $sendfs#book $sendfs@snap3 >$stream"
log_must test -s $stream

log_pass "a meta-dnode hole and a redaction entry for the object it covers"
