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
#	Two apply entries can share a destination locator.  When the
#	snapshot the walk is inside is destroyed, the walk finishes
#	that snapshot's txg range in the next dataset, then walks the
#	dataset again from the snapshot's txg.  A block cloned between
#	the two passes gives a locator the first pass indexed a second
#	entry.  The run skips the old block as stale, redirects the
#	clone, and counts each block once.
#
# STRATEGY:
#	1. Write z and snapshot it, so z's blocks are the source.
#	   Copy z to f in dataset g.
#	2. Copy z many times into dataset x and write as many random
#	   files into dataset d.  Snapshot x@s1 and d@p, and write to
#	   x after x@s1.  d sorts before x in the walk's queue and d@p
#	   after x@s1.
#	3. Export and import, delay every read and turn off the
#	   scan's prefetch, so the walk spends seconds in each
#	   dataset.
#	4. Start a full run.  Pause and resume it until the scan
#	   record a pause writes shows the walk stopped inside x@s1.
#	   Destroy d@p, which queues d, and x@s1, which moves the walk
#	   into x and marks x for a second pass.
#	5. Resume the run.  It finishes x@s1's range in x, then takes
#	   d before its second pass over x.  Pause it inside d.
#	6. Clone f over every copy of z in x, resume the run and let
#	   it finish.
#	7. Check that every old copy was stale and every clone of f
#	   was redirected, so each of those locators had two apply
#	   entries, that f and every clone of f point at z's blocks,
#	   and that saved counts f's blocks once.
#

verify_runnable "global"

# Each file costs the walk one delayed read of its indirect block.
typeset -i nfiles=128
typeset -i delay_ms=250
typeset -i bound=300
typeset -i i tries asz hits
typeset -i x d dp s1 s1c
typeset at again dev

function cleanup
{
	zinject -c all >/dev/null 2>&1
	log_must restore_tunable TXG_TIMEOUT
	log_must restore_tunable NO_SCRUB_PREFETCH
	clonedup_cleanup
}

function cd_objset # dataset
{
	zfs get -Hpo value objsetid $TESTPOOL/$1
}

#
# Where the walk stopped, from the scan record on disk: the dataset,
# then the txg its pass over that dataset starts from.  A clonedup
# walk writes the record only when the scan's state or flags change,
# so a running walk leaves an old bookmark there.  A pause writes the
# bookmark of the txg the walk last stopped in.
#
function cd_walk
{
	clonedup_scan_record bookmark_objset cur_min_txg
}

#
# Pause the run with its walk inside the dataset with id ds,
# resuming it between looks.  A stop that matches the pattern bad
# first fails the test.
#
function cd_pause_in # ds bad
{
	typeset -i end=$((SECONDS + bound))

	while ((SECONDS < end)); do
		log_must zpool clonedup -p $TESTPOOL
		at=$(cd_walk)
		[[ $at == "$1 "* ]] && return 0
		[[ $at == $2 ]] &&
		    log_fail "the walk stopped at '$at' before $1"
		log_must zpool clonedup -f $TESTPOOL
		sleep 2
	done
	log_fail "the walk did not stop inside $1 in $bound seconds"
}

# How many blocks of x point at a block of z.
function cd_x_on_z
{
	typeset zb=$(clonedup_file_blocks $TESTPOOL/q z |
	    awk '{ printf "%s ", $2 }')

	zdb -ddddd $TESTPOOL/x | awk -v zb="$zb" '
	    BEGIN { n = split(zb, w)
		for (i = 1; i <= n; i++) z[w[i]] = 1 }
	    / L0 / && ($3 in z) { hits++ }
	    END { print hits + 0 }'
}

log_assert "two apply entries can share a destination locator"
log_onexit cleanup
log_must save_tunable TXG_TIMEOUT
log_must save_tunable NO_SCRUB_PREFETCH

clonedup_pool_create -O recordsize=4k
log_must zfs create $TESTPOOL/q
log_must dd if=/dev/urandom of=/$TESTPOOL/q/z bs=4k count=2 \
    status=none
clonedup_sync
log_must zfs snapshot $TESTPOOL/q@src
log_must zfs create $TESTPOOL/g
clonedup_dup /$TESTPOOL/q/z /$TESTPOOL/g/f

# The walk takes queued datasets in object number order.  d has to
# come before the second pass over x, so d gets the lower number.
log_must zfs create $TESTPOOL/x
log_must zfs create $TESTPOOL/d
if (( $(cd_objset x) < $(cd_objset d) )); then
	log_must zfs rename $TESTPOOL/x $TESTPOOL/t
	log_must zfs rename $TESTPOOL/d $TESTPOOL/x
	log_must zfs rename $TESTPOOL/t $TESTPOOL/d
fi
for ((i = 0; i < nfiles; i++)); do
	clonedup_dup /$TESTPOOL/q/z /$TESTPOOL/x/c$i
	log_must dd if=/dev/urandom of=/$TESTPOOL/d/r$i bs=4k \
	    count=2 status=none
done
clonedup_sync
log_must zfs snapshot $TESTPOOL/x@s1
# written since x@s1, so the walk takes x's copies as destinations
log_must file_write -o create -f /$TESTPOOL/x/w -b 4096 -c 1 -d R
clonedup_sync
# d@p has to stay queued until the walk is inside x@s1
for ((tries = 0; tries < 20; tries++)); do
	log_must zfs snapshot $TESTPOOL/d@p
	(( $(cd_objset d@p) > $(cd_objset x@s1) )) && break
	log_must zfs destroy $TESTPOOL/d@p
done
(( tries < 20 )) || log_fail "d@p never sorted after x@s1"

asz=$(clonedup_blk_asize $TESTPOOL/g f)
log_must [ $asz -gt 0 ]
hits=$(cd_x_on_z)
log_note "before the run $hits blocks of x point at z's"
log_must [ $hits -eq 0 ]
x=$(cd_objset x)
d=$(cd_objset d)
s1=$(cd_objset x@s1)
s1c=$(zfs get -Hpo value createtxg $TESTPOOL/x@s1)
dp=$(cd_objset d@p)
log_note "x $x, d $d, x@s1 $s1 from txg $s1c, d@p $dp"

# A cold cache and slow reads spread the walk of each dataset over
# many txgs.  With a one second txg timeout the walk stops about
# once a second, and a pause can catch it inside a dataset.
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
for dev in $(get_disklist $TESTPOOL); do
	log_must zinject -d $dev -D $delay_ms:1 -T read $TESTPOOL
done
log_must set_tunable32 TXG_TIMEOUT 1
log_must set_tunable32 NO_SCRUB_PREFETCH 1

log_must zpool clonedup -f $TESTPOOL
# x, d@p and d all come after x@s1 in the walk
cd_pause_in $s1 "@($x|$dp|$d) *"
log_note "paused at $at"

log_must zfs destroy $TESTPOOL/d@p
log_must zfs destroy $TESTPOOL/x@s1
log_must zpool sync $TESTPOOL
at=$(cd_walk)
again=$(clonedup_scan_record flag_visit_ds_again)
log_note "after the destroys: at $at, visit_ds_again '$again'"
[[ $at == "$x "* ]] ||
    log_fail "destroying x@s1 did not move the walk into x"
# the walk owes x a second pass
[[ $again == 1 ]] ||
    log_fail "destroying x@s1 did not mark x for a second pass"

log_must zpool clonedup -f $TESTPOOL
# the second pass over x starts from x@s1's txg
cd_pause_in $d "$x $s1c"
again=$(clonedup_scan_record flag_visit_ds_again)
log_note "paused at $at, visit_ds_again '$again'"
[[ $again == 0 ]] ||
    log_fail "the walk did not finish x@s1's range in x"

for ((i = 0; i < nfiles; i++)); do
	log_must clonefile -f /$TESTPOOL/g/f /$TESTPOOL/x/c$i
done
clonedup_sync
clonedup_check_shared $TESTPOOL/g f $TESTPOOL/x c0 "0 1"
log_must zinject -c all
log_must zpool clonedup -f $TESTPOOL
clonedup_bounded $bound zpool wait -t clonedup $TESTPOOL

clonedup_jstat_is state FINISHED
clonedup_jstat_is errors 0
clonedup_jstat_is clonedup_groups 2
#
# Each block of each copy had two apply entries at its locator: the
# old block, walked once in x@s1 or in x and gone by the apply, and
# the clone of f that the second pass over x found.
#
clonedup_jstat_is clonedup_skipped_stale $((2 * nfiles))
# f and every clone of it, but f's blocks are freed only once
clonedup_jstat_is clonedup_applied $((2 * (nfiles + 1)))
clonedup_jstat_is clonedup_saved $((2 * asz))
clonedup_jstat_is clonedup_saved_snapheld 0

clonedup_check_shared $TESTPOOL/q z $TESTPOOL/g f "0 1"
hits=$(cd_x_on_z)
log_note "after the run $hits blocks of x point at z's"
log_must [ $hits -eq $((2 * nfiles)) ]
log_must cmp /$TESTPOOL/q/z /$TESTPOOL/g/f
for ((i = 0; i < nfiles; i++)); do
	log_must cmp /$TESTPOOL/q/z /$TESTPOOL/x/c$i
done
clonedup_leakcheck

log_pass "two apply entries can share a destination locator"
