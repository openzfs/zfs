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
#	Verify zfs rewrite moves blocks that nopwrite would leave in place.
#
# STRATEGY:
#	1. Create a file on a dataset where nopwrite applies (sha256 checksum
#	   and compression) and sync it.
#	2. Rewrite it and verify that every block moved and has a new
#	   logical birth time.
#	3. Rewrite it with -P and verify that every block moved and kept its
#	   logical birth time.
#	4. Overwrite it with the same data and verify that no block moved.

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset fs=$TESTPOOL/$TESTFS/nopwrite
typeset data=$(mktemp)
typeset tmp=$(mktemp)
typeset before=$(mktemp)
typeset after=$(mktemp)

function cleanup
{
	datasetexists $fs && destroy_dataset $fs
	rm -f $data $tmp $before $after
}

# Save the DVA and logical birth time of each L0 block of the file in $1.
function save_blocks
{
	log_must eval "zdb -Ovv $fs file > $tmp"
	awk '/ L0 / {
		for (i = 4; i <= NF; i++)
			if (sub(/^B=/, "", $i))
				break
		sub(/\/.*/, "", $i)
		print $3, $i
	}' < $tmp > $1
	log_must [ $(awk 'END { print NR }' < $1) -eq 4 ]
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

log_assert "zfs rewrite moves blocks that nopwrite would leave in place"

log_onexit cleanup

log_must zfs create -o recordsize=128k -o checksum=sha256 \
    -o compression=lz4 $fs
typeset file=$(get_prop mountpoint $fs)/file

log_must dd if=/dev/urandom of=$data bs=128k count=4
log_must dd if=$data of=$file bs=128k
log_must sync_pool $TESTPOOL
save_blocks $before

log_must zfs rewrite $file
log_must sync_pool $TESTPOOL
save_blocks $after
verify_blocks 4 4

log_must zfs rewrite -P $file
log_must sync_pool $TESTPOOL
save_blocks $after
verify_blocks 4 0

# Writes of unchanged data still keep their blocks in place.
log_must dd if=$data of=$file bs=128k conv=notrunc
log_must sync_pool $TESTPOOL
save_blocks $after
verify_blocks 0 0

log_must cmp $data $file

log_pass "zfs rewrite moves blocks that nopwrite would leave in place"
