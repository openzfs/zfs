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
#	An all-zero block becomes a hole, as a hole punch makes one,
#	so it needs neither matching properties nor room under a
#	quota.  A dry run counts the holes the real run makes, the
#	surviving copy's included, whichever destinations the real
#	run refuses.
#
# STRATEGY:
#	1. Write one zero block first, so it survives as the source.
#	2. Write zero blocks into an immutable file, into a dataset
#	   whose compression changes afterwards, and into a file whose
#	   owner is over quota.
#	3. Run a clonedup dry run, and check that every block still
#	   holds data.
#	4. Run a clonedup, and check that every zero block outside the
#	   immutable file is a hole.
#	5. Check that the dry run counted the blocks and bytes that
#	   the real run punched.
#

verify_runnable "global"

function cleanup
{
	if is_freebsd; then
		chflags noschg /$TESTPOOL/imm/z 2>/dev/null
	else
		chattr -i /$TESTPOOL/imm/z 2>/dev/null
	fi
	clonedup_cleanup
}

# How many of a file's blocks hold data rather than a hole.
function cd_data_blocks # ds file
{
	zdb -vvvvv $1 -O $2 | awk '/ L0 / && /L\/[0-9a-f]+P/ { n++ }
	    END { print n + 0 }'
}

# Check how many blocks of s/s, imm/z, lz/z and q/z hold data.
function cd_check_data # s imm lz q
{
	typeset -i got want
	typeset f

	for f in s/s:$1 imm/z:$2 lz/z:$3 q/z:$4; do
		want=${f#*:}
		f=${f%:*}
		got=$(cd_data_blocks $TESTPOOL/${f%/*} ${f#*/})
		log_note "$f: $got data blocks, want $want"
		log_must [ $got -eq $want ]
	done
}

log_assert "a zero block becomes a hole whatever its properties"
log_onexit cleanup

clonedup_pool_create
typeset ds
for ds in s imm lz q; do
	log_must zfs create $TESTPOOL/$ds
done
log_must dd if=/dev/zero of=/$TESTPOOL/s/s bs=$CD_BS count=1 \
    status=none
clonedup_sync
for ds in imm lz q; do
	log_must dd if=/dev/zero of=/$TESTPOOL/$ds/z bs=$CD_BS \
	    count=$CD_BLOCKS status=none
done
log_must chown nobody /$TESTPOOL/q/z
clonedup_sync

log_must zfs set compression=lz4 $TESTPOOL/lz
log_must zfs set userquota@nobody=512K $TESTPOOL/q
if is_freebsd; then
	log_must chflags schg /$TESTPOOL/imm/z
else
	log_must chattr +i /$TESTPOOL/imm/z
fi
clonedup_sync

typeset -i asz=$(clonedup_blk_asize $TESTPOOL/s s)
log_must [ $asz -gt 0 ]

clonedup_run -n
log_must eval "zpool status -j $TESTPOOL | grep -q '\"dry_run\":true'"
typeset dcand=$(clonedup_jstat clonedup_candidates)
typeset dapplied=$(clonedup_jstat clonedup_applied)
typeset dsaved=$(clonedup_jstat clonedup_saved)
cd_check_data 1 $CD_BLOCKS $CD_BLOCKS $CD_BLOCKS

clonedup_run
typeset applied=$(clonedup_jstat clonedup_applied)
typeset saved=$(clonedup_jstat clonedup_saved)
typeset punched=$(clonedup_kstat punched)
log_note "dry run: $dcand candidates, $dapplied blocks," \
    "$dsaved reclaimable"
log_note "run: $applied blocks, $saved saved, kstat punched" \
    "$punched; zero block asize $asz"
typeset v
for v in "$dcand" "$dapplied" "$dsaved" "$applied" "$saved" \
    "$punched"; do
	[[ $v == +([0-9]) ]] || log_fail "not a count: '$v'"
done

clonedup_sync
cd_check_data 0 $CD_BLOCKS 0 0

# lz/z and q/z, then the source s/s
typeset -i want=$((2 * CD_BLOCKS + 1))
log_must [ $applied -eq $want ]
log_must [ $saved -eq $((want * asz)) ]
log_must [ $punched -eq $want ]
log_must [ $dapplied -eq $applied ]
log_must [ $dsaved -eq $saved ]
# candidates are destinations, and the source's punch is not one
log_must [ $dcand -eq $((2 * CD_BLOCKS)) ]
clonedup_leakcheck

log_pass "a zero block becomes a hole whatever its properties"
