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
#	A dry run reports what a real run would save, so it refuses
#	the pairs a real run refuses.  Those are a pair across
#	datasets whose compression or checksum property differs, a
#	pair whose owner is over quota, and a pair of immutable
#	files.  The dry run's candidates and reclaimable bytes equal
#	the real run's applied blocks and saved bytes on the same
#	data.
#
# STRATEGY:
#	1. Write a file and a byte copy of it into the pool's root.
#	2. Write a file into a compression=lz4 dataset and a byte copy
#	   into the root.  The data does not compress, so both copies
#	   are stored uncompressed and pair.
#	3. Write a file and a byte copy into two sha256 datasets, and
#	   set the second to sha512.
#	4. Write a file and a byte copy owned by a user over quota.
#	5. Write a file and a byte copy and make both immutable.
#	6. Run a clonedup dry run, then a clonedup.
#	7. Check that the real run cloned the first pair alone, and
#	   that the dry run counted the same blocks, bytes and skips.
#

verify_runnable "global"

function cleanup
{
	typeset f

	for f in a b; do
		if is_freebsd; then
			chflags noschg /$TESTPOOL/imm/$f 2>/dev/null
		else
			chattr -i /$TESTPOOL/imm/$f 2>/dev/null
		fi
	done
	clonedup_cleanup
}

log_assert "a dry run refuses the pairs a real run refuses"
log_onexit cleanup

clonedup_pool_create
typeset all="$(clonedup_all_blocks)"

# the pair the real run clones
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b

# EXDEV, compression differs
log_must zfs create -o compression=lz4 $TESTPOOL/lz
clonedup_write /$TESTPOOL/lz/c
clonedup_dup /$TESTPOOL/lz/c /$TESTPOOL/c

# EXDEV, checksum differs; sha256 pairs skip the verify read
log_must zfs create -o checksum=sha256 $TESTPOOL/k1
log_must zfs create -o checksum=sha256 $TESTPOOL/k2
clonedup_write /$TESTPOOL/k1/k
clonedup_dup /$TESTPOOL/k1/k /$TESTPOOL/k2/k

# EDQUOT, the owner of both copies is over quota
log_must zfs create $TESTPOOL/q
clonedup_write /$TESTPOOL/q/a
clonedup_dup /$TESTPOOL/q/a /$TESTPOOL/q/b
log_must chown nobody /$TESTPOOL/q/a /$TESTPOOL/q/b

# EPERM, both copies are immutable
log_must zfs create $TESTPOOL/imm
clonedup_write /$TESTPOOL/imm/a
clonedup_dup /$TESTPOOL/imm/a /$TESTPOOL/imm/b
clonedup_sync

log_must zfs set checksum=sha512 $TESTPOOL/k2
log_must zfs set userquota@nobody=512K $TESTPOOL/q
if is_freebsd; then
	log_must chflags schg /$TESTPOOL/imm/a /$TESTPOOL/imm/b
else
	log_must chattr +i /$TESTPOOL/imm/a /$TESTPOOL/imm/b
fi
clonedup_sync

typeset -i asz=$(clonedup_blk_asize $TESTPOOL a)
log_must [ $asz -gt 0 ]

clonedup_run -n
log_must eval "zpool status -j $TESTPOOL | grep -q '\"dry_run\":true'"
typeset dcand=$(clonedup_jstat clonedup_candidates)
typeset dapplied=$(clonedup_jstat clonedup_applied)
typeset dsaved=$(clonedup_jstat clonedup_saved)
typeset dskipped=$(clonedup_jstat clonedup_skipped)
clonedup_check_shared $TESTPOOL a $TESTPOOL b ""

clonedup_run
typeset applied=$(clonedup_jstat clonedup_applied)
typeset saved=$(clonedup_jstat clonedup_saved)
typeset skipped=$(clonedup_jstat clonedup_skipped)
log_note "dry run: $dcand candidates, $dapplied blocks," \
    "$dsaved reclaimable, $dskipped skipped"
log_note "run: $applied blocks, $saved saved, $skipped skipped;" \
    "block asize $asz"
typeset v
for v in "$dcand" "$dapplied" "$dsaved" "$dskipped" \
    "$applied" "$saved" "$skipped"; do
	[[ $v == +([0-9]) ]] || log_fail "not a count: '$v'"
done

# the real run refused every pair but the first
log_must [ $applied -eq $CD_BLOCKS ]
log_must [ $saved -eq $((CD_BLOCKS * asz)) ]
log_must [ $skipped -ge $((4 * CD_BLOCKS)) ]
# and the dry run counted what the real run did
log_must [ $dcand -eq $applied ]
log_must [ $dsaved -eq $saved ]
log_must [ $dapplied -eq $applied ]
log_must [ $dskipped -eq $skipped ]

clonedup_sync
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$all"
clonedup_check_shared $TESTPOOL/lz c $TESTPOOL c ""
clonedup_check_shared $TESTPOOL/k1 k $TESTPOOL/k2 k ""
clonedup_check_shared $TESTPOOL/q a $TESTPOOL/q b ""
clonedup_check_shared $TESTPOOL/imm a $TESTPOOL/imm b ""
clonedup_leakcheck

log_pass "a dry run refuses the pairs a real run refuses"
