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
#
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib

#
# DESCRIPTION:
#	All-zero blocks become holes under a checksum strong enough
#	for dedup too.  The run trusts a match under such a checksum
#	and never reads the pair, so the checksum alone has to show
#	that the blocks hold zeros.  Skein and BLAKE3 mix the pool's
#	salt into theirs.  Equal blocks that hold data still clone.
#
# STRATEGY:
#	1. Create a dataset each with checksum=sha256, skein and
#	   blake3, compression off, and write two all-zero files
#	   into each.
#	2. Check that every block of the files holds data, and that
#	   all of a dataset's blocks carry one checksum.
#	3. Run a clonedup dry run, then a clonedup.
#	4. Check that the dry run left every block in place.
#	5. Check that every block of the files is now a hole, the
#	   source of each group included, that the BRT is empty, and
#	   that no block was read to verify it.
#	6. Check that the dry run counted the same blocks and bytes
#	   as the run.
#	7. Write a random file and a byte copy of it into each
#	   dataset and run clonedup again.  Check that the copy
#	   shares the original's blocks and that they hold data.
#

verify_runnable "global"

log_assert "all-zero blocks become holes under a dedup checksum"
log_onexit clonedup_cleanup

typeset all="$(clonedup_all_blocks)"
typeset cks="sha256 skein blake3"
typeset -i ncks=3
typeset -i want_saved=0
typeset ck f out v

#
# How many of a file's L0 block pointers hold data, then how many
# "Indirect blocks:" headers zdb printed for it, which is 1 when zdb
# walked the file at all.  zdb prints no checksum for a hole.
#
function cd_blocks # ds file
{
	zdb -vvvvv $1 -O $2 | awk '
	    /^Indirect blocks:/ { h++ }
	    / L0 / && /cksum=/ { n++ }
	    END { print n + 0, h + 0 }'
}

# The checksum of each L0 block of a file that holds data.
function cd_cksums # ds file
{
	zdb -vvvvv $1 -O $2 | awk '/ L0 / && /cksum=/ { print $7 }'
}

clonedup_pool_create
for ck in $cks; do
	log_must zfs create -o checksum=$ck $TESTPOOL/$ck
	log_must [ "$(get_prop compression $TESTPOOL/$ck)" = "off" ]
	for f in z1 z2; do
		log_must dd if=/dev/zero of=/$TESTPOOL/$ck/$f \
		    bs=$CD_BS count=$CD_BLOCKS status=none
	done
done
clonedup_sync

for ck in $cks; do
	for f in z1 z2; do
		out=$(cd_blocks $TESTPOOL/$ck $f)
		log_note "$ck/$f before: data blocks, headers: $out"
		log_must [ "$out" = "$CD_BLOCKS 1" ]
	done
	# both files hold the same zero blocks under one checksum
	out=$(cd_cksums $TESTPOOL/$ck z1; cd_cksums $TESTPOOL/$ck z2)
	out=$(echo "$out" | sort -u)
	log_note "$ck zero block checksum: $out"
	[[ $out == cksum=+([0-9a-f:]) ]] ||
	    log_fail "$ck: want one checksum for all zero blocks"
	# a punched source and its copies each free one block
	v=$(clonedup_blk_asize $TESTPOOL/$ck z1)
	log_must [ $v -gt 0 ]
	(( want_saved += 2 * CD_BLOCKS * v ))
done

clonedup_run -n
log_must eval "zpool status -j $TESTPOOL | grep -q '\"dry_run\":true'"
typeset dry_applied=$(clonedup_jstat clonedup_applied)
typeset dry_saved=$(clonedup_jstat clonedup_saved)
for ck in $cks; do
	for f in z1 z2; do
		out=$(cd_blocks $TESTPOOL/$ck $f)
		log_note "$ck/$f after the dry run: $out"
		log_must [ "$out" = "$CD_BLOCKS 1" ]
	done
done

clonedup_run
typeset applied=$(clonedup_jstat clonedup_applied)
typeset saved=$(clonedup_jstat clonedup_saved)
clonedup_sync

for ck in $cks; do
	for f in z1 z2; do
		out=$(cd_blocks $TESTPOOL/$ck $f)
		log_note "$ck/$f after the run: $out"
		log_must [ "$out" = "0 1" ]
		v=$(du -k /$TESTPOOL/$ck/$f | cut -f1)
		log_must [ $v -lt 64 ]
		log_must [ $(stat_size /$TESTPOOL/$ck/$f) -eq \
		    $((CD_BLOCKS * CD_BS)) ]
	done
	log_must cmp /$TESTPOOL/$ck/z1 /$TESTPOOL/$ck/z2
done
log_must [ $(zpool get -Hpo value bcloneused $TESTPOOL) -eq 0 ]
clonedup_kstat_is punched $((ncks * 2 * CD_BLOCKS))
clonedup_kstat_is clones 0
clonedup_kstat_is verify_reads 0

for v in "$dry_applied" "$dry_saved" "$applied" "$saved"; do
	[[ $v == +([0-9]) ]] || log_fail "not a count: '$v'"
done
log_note "run: $applied blocks, $saved saved; dry run:" \
    "$dry_applied blocks, $dry_saved saved; want" \
    "$((ncks * 2 * CD_BLOCKS)) blocks, $want_saved saved"
log_must [ $applied -eq $((ncks * 2 * CD_BLOCKS)) ]
log_must [ $saved -eq $want_saved ]
log_must [ $dry_applied -eq $applied ]
log_must [ $dry_saved -eq $saved ]

for ck in $cks; do
	clonedup_write /$TESTPOOL/$ck/r1
	clonedup_dup /$TESTPOOL/$ck/r1 /$TESTPOOL/$ck/r2
done
clonedup_sync
clonedup_run
clonedup_sync

for ck in $cks; do
	for f in r1 r2; do
		out=$(cd_blocks $TESTPOOL/$ck $f)
		log_note "$ck/$f: data blocks, headers: $out"
		log_must [ "$out" = "$CD_BLOCKS 1" ]
	done
	clonedup_check_shared $TESTPOOL/$ck r1 $TESTPOOL/$ck r2 "$all"
done
log_must [ $(zpool get -Hpo value bcloneused $TESTPOOL) -gt 0 ]
clonedup_kstat_is punched $((ncks * 2 * CD_BLOCKS))
clonedup_kstat_is clones $((ncks * CD_BLOCKS))
clonedup_kstat_is verify_reads 0
clonedup_leakcheck

log_pass "all-zero blocks become holes under a dedup checksum"
