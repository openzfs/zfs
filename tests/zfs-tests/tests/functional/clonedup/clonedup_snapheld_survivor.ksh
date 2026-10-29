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
#	A copy held by a snapshot survives over a live copy whose BRT
#	range already holds an entry.  DCE_F_MAYBE_SHARED says only
#	that some block in the copy's range of its vdev is in the BRT,
#	so it only breaks ties between copies that a snapshot holds
#	alike.  The run redirects the live copy onto the snapshot's
#	block, and the pool gets back the space the live copy used.
#
# STRATEGY:
#	1. Write S in dataset b, then in the same txg unique filler
#	   of two BRT ranges per top-level vdev, and snapshot b.  The
#	   head keeps S.
#	2. Write L, a copy of S, in dataset a, and clone it to L2, so
#	   that L's range holds a BRT entry wherever L landed.
#	3. Check with zdb that every range L is in holds a BRT entry
#	   and no range S is in holds one.
#	4. Run clonedup.  L and L2 share S's blocks in the snapshot,
#	   and the pool gets back the space L used.
#

verify_runnable "global"

typeset src=$TEST_BASE_DIR/clonedup_snapheld_src

function cleanup
{
	rm -f $src
	clonedup_cleanup
}

log_assert "a snapshot-held copy survives over a live BRT-shared copy"
log_onexit cleanup

# BRT_RANGESIZE in brt_impl.h: one brt_maybe_exists() answer covers
# a range this large of one top-level vdev.
typeset -ri RANGE=16777216
typeset all="$(clonedup_all_blocks)"

#
# The BRT range of each DVA on stdin, as vdev:range.  zdb prints a
# block's DVA as vdev:offset:asize and a BRT entry as vdev:offset,
# with the offset in hex.
#
function dva_ranges
{
	typeset dva off

	while read dva; do
		off=${dva#*:}
		off=${off%%:*}
		echo "${dva%%:*}:$((16#$off / RANGE))"
	done | sort -u
}

clonedup_pool_create
log_must zfs create $TESTPOOL/a
log_must zfs create $TESTPOOL/b

#
# A metaslab starts at a multiple of its size, 2^metaslab_shift, and
# is never smaller than SPA_MAXBLOCKSIZE, which is one range.  Blocks
# in different metaslabs are then in different ranges.  Within one
# metaslab an allocator places a txg's blocks in order, so filler
# written in S's txg pushes later blocks past S's range on every
# top-level vdev.
#
typeset -a shifts=($(zdb -C $TESTPOOL |
    awk '$1 == "metaslab_shift:" { print $2 }'))
typeset -i nvdevs=${#shifts[@]}
typeset -i ms

(( nvdevs > 0 )) || log_fail "zdb -C $TESTPOOL shows no metaslabs"
for ms in ${shifts[@]}; do
	log_note "metaslab size $((1 << ms)), BRT range $RANGE"
	(( (1 << ms) >= RANGE )) ||
	    log_fail "metaslab size $((1 << ms)) is under a range"
done
typeset -i fill
((fill = 2 * RANGE * nvdevs / CD_BS))
log_note "$nvdevs top-level vdevs, $fill filler blocks"

# S and the filler go into one file straight after a sync, so the
# same txg holds both.
clonedup_write $src
clonedup_sync
clonedup_dup $src /$TESTPOOL/b/S
log_must dd if=/dev/urandom of=/$TESTPOOL/b/S bs=$CD_BS \
    seek=$CD_BLOCKS count=$fill conv=notrunc status=none
clonedup_sync
log_must zfs snapshot $TESTPOOL/b@s

# L in a, and its clone L2 once L is on disk.
clonedup_dup $src /$TESTPOOL/a/L
clonedup_sync
log_must clonefile -f /$TESTPOOL/a/L /$TESTPOOL/a/L2
clonedup_sync
clonedup_check_shared $TESTPOOL/a L $TESTPOOL/a L2 "$all"

#
# The walk sets DCE_F_MAYBE_SHARED on a block whose range holds a BRT
# entry.  Every range L is in must hold one, and no range S is in.
#
typeset s_dva=$(clonedup_file_blocks $TESTPOOL/b@s S |
    awk -v n=$CD_BLOCKS '$1 < n { print $2 }')
typeset l_dva=$(clonedup_file_blocks $TESTPOOL/a L |
    awk '{ print $2 }')
typeset b_dva=$(zdb -TTTT $TESTPOOL |
    awk '$1 ~ /^[0-9]+:[0-9a-f]+$/ { print $1 }')
typeset -i ns=$(echo $s_dva | wc -w)
typeset -i nl=$(echo $l_dva | wc -w)

log_note "S blocks: $(echo $s_dva)"
log_note "L blocks: $(echo $l_dva)"
log_note "BRT entries: $(echo $b_dva)"
(( ns == CD_BLOCKS && nl == CD_BLOCKS )) ||
    log_fail "zdb listed $ns blocks of S and $nl of L"
[[ -n $b_dva ]] || log_fail "zdb -TTTT $TESTPOOL listed no BRT entry"

typeset s_ranges=$(echo $(echo "$s_dva" | dva_ranges))
typeset l_ranges=$(echo $(echo "$l_dva" | dva_ranges))
typeset b_ranges=$(echo $(echo "$b_dva" | dva_ranges))
typeset r

log_note "ranges: S '$s_ranges', L '$l_ranges', BRT '$b_ranges'"
for r in $l_ranges; do
	[[ " $b_ranges " == *" $r "* ]] ||
	    log_fail "L's range $r holds no BRT entry"
done
for r in $s_ranges; do
	[[ " $b_ranges " == *" $r "* ]] &&
	    log_fail "the setup did not separate the regions:" \
	    "S's range $r holds a BRT entry"
done

typeset -i lsize=0 asize
typeset d

for d in $l_dva; do
	asize=16#${d##*:}
	((lsize += asize))
done
typeset before=$(zpool get -Hpo value allocated $TESTPOOL)

clonedup_run
# Freed space stays in allocated until it leaves the defer tree,
# two txgs after the free.
for i in 1 2 3; do
	clonedup_sync
done
typeset after=$(zpool get -Hpo value allocated $TESTPOOL)

clonedup_check_shared $TESTPOOL/b@s S $TESTPOOL/a L "$all"
clonedup_check_shared $TESTPOOL/b@s S $TESTPOOL/a L2 "$all"
log_must cmp $src /$TESTPOOL/a/L
log_must cmp $src /$TESTPOOL/a/L2

#
# Nothing refers to L's blocks now, so the run freed them.  A blank
# reading must not pass, so both must be numbers.  The drop must be
# at least three quarters of what L used, which leaves room for the
# metadata the run writes.
#
log_note "allocated before $before, after $after; L used $lsize"
[[ $before == +([0-9]) && $after == +([0-9]) ]] ||
    log_fail "allocated not measured: '$before' '$after'"
(( lsize > 0 )) || log_fail "the size of L not measured"
(( before - after >= lsize - lsize / 4 )) ||
    log_fail "the run freed $((before - after)) bytes, want at" \
    "least $((lsize - lsize / 4)) of L's $lsize"
clonedup_jstat_is errors 0
clonedup_leakcheck

log_pass "a snapshot-held copy survives over a live BRT-shared copy"
