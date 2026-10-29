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
#	A snapshot taken after the walk and before the apply holds the
#	old blocks of the destinations it covers, and the apply sees
#	it.  A redirect there stays within the quota and counts as
#	saved only while the snapshot lives, a hole punched there
#	counts the same way, and an all-zero source the snapshot holds
#	stays whole.  A dry run counts what the run does.
#
# STRATEGY:
#	1. Write two files into s.  Copy the first into q and the
#	   second into c.  Write an all-zero block into z and copy it
#	   there.  Leave q a quota with room for the growth of one
#	   redirect.
#	2. Park a dry run in its apply phase, snapshot q, c and z, and
#	   let it finish.  Check its counts, and that it changed
#	   nothing.
#	3. Destroy those snapshots and do the same with a run.
#	4. Check that q is within its quota with one block
#	   redirected, that c's copy shares every block, that the zero
#	   copy is a hole and its source is not, and that no file
#	   changed.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_ENABLED
	clonedup_cleanup
}

# How many of a file's blocks hold data rather than a hole.
function sw_data_blocks # ds file
{
	zdb -vvvvv $1 -O $2 | awk '/ L0 / && /L\/[0-9a-f]+P/ { n++ }
	    END { print n + 0 }'
}

#
# Park a run with the flags given in its apply phase, snapshot q, c
# and z under the name given, and let the run finish.  Every old
# block it replaces is then held by a snapshot.  It redirects one
# block of q and all of c, refuses the rest of q under the quota,
# punches z/b, and leaves z/a alone and counts it as stale.
#
function sw_round # snap [flags]
{
	typeset ds

	log_must set_tunable32 CLONEDUP_APPLY_ENABLED 0
	log_must zpool clonedup $2 $TESTPOOL
	clonedup_wait_applying
	# A write after each snapshot, so each dataset has changed.
	# Each names its dataset, so no later round pairs two of them.
	for ds in q c z; do
		log_must zfs snapshot $TESTPOOL/$ds@$1
		log_must eval "echo $1 $ds > /$TESTPOOL/$ds/w$1"
	done
	clonedup_sync
	log_must set_tunable32 CLONEDUP_APPLY_ENABLED 1
	log_must zpool wait -t clonedup $TESTPOOL

	clonedup_jstat_is errors 0
	clonedup_jstat_is clonedup_applied $((CD_BLOCKS + 2))
	clonedup_jstat_is clonedup_skipped_policy $((CD_BLOCKS - 1))
	clonedup_jstat_is clonedup_skipped_stale 1
	# clonedup_saved is the total, and a snapshot holds all of it
	typeset -i held
	((held = (CD_BLOCKS + 1) * asz + zasz))
	clonedup_jstat_is clonedup_saved $held
	clonedup_jstat_is clonedup_saved_snapheld $held
}

log_assert "a snapshot taken during the apply holds what it covers"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_ENABLED

clonedup_pool_create
typeset ds
typeset -i nq
for ds in s q c z; do
	log_must zfs create $TESTPOOL/$ds
done
clonedup_write /$TESTPOOL/s/a
clonedup_write /$TESTPOOL/s/b
log_must dd if=/dev/zero of=/$TESTPOOL/z/a bs=$CD_BS count=1 \
    status=none
clonedup_sync
clonedup_dup /$TESTPOOL/s/a /$TESTPOOL/q/a
clonedup_dup /$TESTPOOL/s/b /$TESTPOOL/c/b
clonedup_dup /$TESTPOOL/z/a /$TESTPOOL/z/b
clonedup_sync

typeset -i asz=$(clonedup_blk_asize $TESTPOOL/q a)
typeset -i zasz=$(clonedup_blk_asize $TESTPOOL/z b)
log_must [ $asz -gt 0 -a $zasz -gt 0 ]
# the deflated size a redirect adds to USED, see
# clonedup_quota_snapheld
typeset -i dsz=$(( (asz >> 9) * ((1 << 17) / (asz >> 9)) ))
# one redirect fits, with the rest of a block left for metadata
typeset -i qused=$(get_prop used $TESTPOOL/q)
typeset -i quota
((quota = qused + 2 * dsz - 4096))
log_must zfs set quota=$quota $TESTPOOL/q
clonedup_sync
log_note "q uses $qused with quota $quota, block asize $asz," \
    "dsize $dsz"

sw_round d -n
clonedup_jstat_is clonedup_flags.dry_run true
clonedup_check_shared $TESTPOOL/s a $TESTPOOL/q a ""
clonedup_check_shared $TESTPOOL/s b $TESTPOOL/c b ""
log_must [ $(sw_data_blocks $TESTPOOL/z a) -eq 1 ]
log_must [ $(sw_data_blocks $TESTPOOL/z b) -eq 1 ]
for ds in q c z; do
	log_must zfs destroy $TESTPOOL/$ds@d
done

sw_round r
qused=$(get_prop used $TESTPOOL/q)
log_note "q uses $qused after the run, quota $quota"
log_must [ $qused -le $quota ]
nq=$(clonedup_shared $TESTPOOL/s a $TESTPOOL/q a | wc -w)
log_note "q/a shares $nq block(s) with s/a, want 1"
log_must [ $nq -eq 1 ]
clonedup_check_shared $TESTPOOL/s b $TESTPOOL/c b \
    "$(clonedup_all_blocks)"
log_must [ $(sw_data_blocks $TESTPOOL/z a) -eq 1 ]
log_must [ $(sw_data_blocks $TESTPOOL/z b) -eq 0 ]
log_must cmp /$TESTPOOL/s/a /$TESTPOOL/q/a
log_must cmp /$TESTPOOL/s/b /$TESTPOOL/c/b
log_must cmp /$TESTPOOL/z/a /$TESTPOOL/z/b
clonedup_leakcheck

log_pass "a snapshot taken during the apply holds what it covers"
