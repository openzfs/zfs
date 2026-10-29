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
#	A run rewrites no block of an immutable file, and none of a
#	file whose owner is over its user quota, whether the
#	filesystem is mounted or the run owns it because nothing has
#	it mounted.  Each refused block counts as a policy skip.  An
#	append-only file is rewritten.  Its content stays the same,
#	and FIDEDUPERANGE rewrites one too.  A dry run counts what
#	the run does.
#
# STRATEGY:
#	1. Make six filesystems, each holding a file and a byte copy
#	   of it.  The pair is immutable, append-only, or owned by a
#	   user over quota, in three mounted filesystems and again in
#	   three unmounted ones.
#	2. For each filesystem alone, with clonedup=off on the rest,
#	   run a full dry run and then a full run.  Check the counts
#	   of both runs, the blocks of both files, and the kind of
#	   handle the runs opened.
#	3. Run over the four refused pairs together and check that
#	   their policy skips add up.
#	4. Mount everything and check that no file's content changed.
#

verify_runnable "global"

typeset -r CASES="mimm mapp mquo oimm oapp oquo"
typeset -r REFUSED="mimm mquo oimm oquo"
typeset DCAND DAPPLIED DSAVED DSKIPPED

function cleanup
{
	typeset c

	for c in $CASES; do
		datasetexists $TESTPOOL/$c || continue
		zfs mount $TESTPOOL/$c 2>/dev/null
		if is_freebsd; then
			chflags noschg,nosappnd /$TESTPOOL/$c/[ab] \
			    2>/dev/null
		else
			chattr -i -a /$TESTPOOL/$c/[ab] 2>/dev/null
		fi
	done
	clonedup_cleanup
}

# Note a count and fail unless it equals want.
function policy_is # what got want
{
	log_note "$1: $2 (want $3)"
	clonedup_assert_counts "$2" "$3"
	log_must [ "$2" -eq "$3" ]
}

# Note a count and fail unless it is above floor.
function policy_gt # what got floor
{
	log_note "$1: $2 (want more than $3)"
	clonedup_assert_counts "$2" "$3"
	log_must [ "$2" -gt "$3" ]
}

# Note a field of zpool status -j and check it.
function policy_field # what key want
{
	policy_is "$1" "$(clonedup_jstat $2)" "$3"
}

# Note a string and fail unless it is not empty and equals want.
function policy_same # what got want
{
	log_note "$1: '$2' (want '$3')"
	[[ -n $2 ]] || log_fail "$1: empty"
	log_must [ "$2" = "$3" ]
}

# A file's L0 blocks on one line, read with zdb, mounted or not.
function policy_blocks # ds file
{
	echo $(clonedup_file_blocks $1 $2)
}

# Make files immutable or append-only.
function policy_flag # immutable|append file...
{
	typeset how=$1

	shift
	if [[ $how == immutable ]]; then
		if is_freebsd; then
			log_must chflags schg "$@"
		else
			log_must chattr +i "$@"
		fi
	elif is_freebsd; then
		log_must chflags sappnd "$@"
	else
		log_must chattr +a "$@"
	fi
}

# Turn clonedup on for the cases named and off for the rest.
function policy_enable # case...
{
	typeset c v

	for c in $CASES; do
		v=off
		[[ " $* " == *" $c "* ]] && v=on
		log_must zfs set clonedup=$v $TESTPOOL/$c
	done
}

# A full dry run, with its counts kept for policy_run.
function policy_dry_run
{
	clonedup_run -f -n
	log_must eval "zpool status -j $TESTPOOL |" \
	    "grep -q '\"dry_run\":true'"
	DCAND=$(clonedup_jstat clonedup_candidates)
	DAPPLIED=$(clonedup_jstat clonedup_applied)
	DSAVED=$(clonedup_jstat clonedup_saved)
	DSKIPPED=$(clonedup_jstat clonedup_skipped)
}

#
# A full run after policy_dry_run.  It refuses skips blocks and
# rewrites napp, each of asize bytes, and the dry run counted the
# same.
#
function policy_run # what skips napp asize
{
	typeset cand applied saved skipped

	clonedup_run -f
	cand=$(clonedup_jstat clonedup_candidates)
	applied=$(clonedup_jstat clonedup_applied)
	saved=$(clonedup_jstat clonedup_saved)
	skipped=$(clonedup_jstat clonedup_skipped)

	policy_field "$1: policy skips" clonedup_skipped_policy $2
	policy_field "$1: stale skips" clonedup_skipped_stale 0
	policy_field "$1: dirty skips" clonedup_skipped_dirty 0
	policy_field "$1: differing skips" clonedup_skipped_differs 0
	policy_field "$1: busy skips" clonedup_skipped_busy 0
	policy_field "$1: errors" errors 0
	policy_is "$1: skipped" "$skipped" $2
	policy_is "$1: candidates" "$cand" $3
	policy_is "$1: applied" "$applied" $3
	policy_is "$1: saved" "$saved" $(($3 * $4))
	policy_is "$1: dry run candidates" "$DCAND" "$cand"
	policy_is "$1: dry run applied" "$DAPPLIED" "$applied"
	policy_is "$1: dry run saved" "$DSAVED" "$saved"
	policy_is "$1: dry run skipped" "$DSKIPPED" "$skipped"
}

#
# Which kind of handle the runs of one case opened, from the kstat
# values before them: owned for an unmounted filesystem and mounted
# for a mounted one.  A mounted immutable file is refused before its
# handle counts.
#
function policy_opened # case mounted owned
{
	typeset km=$(clonedup_kstat dst_mounted)
	typeset ko=$(clonedup_kstat dst_owned)

	clonedup_assert_counts "$km" "$ko" "$2" "$3"
	case $1 in
	o*)
		policy_is "$1: mounted opens" $((km - $2)) 0
		policy_gt "$1: owned opens" $((ko - $3)) 0
		;;
	mimm)
		policy_is "$1: mounted opens" $((km - $2)) 0
		policy_is "$1: owned opens" $((ko - $3)) 0
		;;
	*)
		policy_gt "$1: mounted opens" $((km - $2)) 0
		policy_is "$1: owned opens" $((ko - $3)) 0
		;;
	esac
}

log_assert "immutable and over-quota files are refused, owned or not"
log_onexit cleanup

clonedup_pool_create
typeset all="$(clonedup_all_blocks)"
typeset -A ha hb ba bb
typeset c ds asz km ko

for c in $CASES; do
	ds=$TESTPOOL/$c
	log_must zfs create $ds
	clonedup_write /$ds/a
	clonedup_sync
	clonedup_dup /$ds/a /$ds/b
	if [[ $c == *quo ]]; then
		log_must chown nobody /$ds/a /$ds/b
	fi
	clonedup_sync
	ha[$c]=$(xxh128digest /$ds/a)
	hb[$c]=$(xxh128digest /$ds/b)
	policy_same "$c: content of the copy" "${hb[$c]}" "${ha[$c]}"
	case $c in
	*imm)
		policy_flag immutable /$ds/a /$ds/b
		log_mustnot eval "echo x > /$ds/b"
		;;
	*app)
		policy_flag append /$ds/a /$ds/b
		log_mustnot eval "echo x > /$ds/b"
		;;
	*quo)
		log_must zfs set userquota@nobody=512K $ds
		policy_gt "$c: usage of nobody" \
		    "$(get_prop userused@nobody $ds)" \
		    "$(get_prop userquota@nobody $ds)"
		;;
	esac
done
clonedup_sync
for c in $CASES; do
	if [[ $c == o* ]]; then
		log_must zfs unmount $TESTPOOL/$c
	fi
done

for c in $CASES; do
	ds=$TESTPOOL/$c
	policy_enable $c
	if [[ $c == o* ]]; then
		policy_same "$c: mounted" "$(get_prop mounted $ds)" no
	fi
	ba[$c]=$(policy_blocks $ds a)
	bb[$c]=$(policy_blocks $ds b)
	asz=$(clonedup_blk_asize $ds a)
	policy_gt "$c: block asize" "$asz" 0
	km=$(clonedup_kstat dst_mounted)
	ko=$(clonedup_kstat dst_owned)

	policy_dry_run
	policy_same "$c: a after the dry run" \
	    "$(policy_blocks $ds a)" "${ba[$c]}"
	policy_same "$c: b after the dry run" \
	    "$(policy_blocks $ds b)" "${bb[$c]}"
	if [[ $c == *app ]]; then
		policy_run $c 0 $CD_BLOCKS $asz
		clonedup_check_shared $ds a $ds b "$all"
	else
		policy_run $c $CD_BLOCKS 0 $asz
		policy_same "$c: a after the run" \
		    "$(policy_blocks $ds a)" "${ba[$c]}"
		policy_same "$c: b after the run" \
		    "$(policy_blocks $ds b)" "${bb[$c]}"
	fi
	policy_opened $c "$km" "$ko"
done

policy_enable $REFUSED
for c in $REFUSED; do
	if [[ $c == o* ]]; then
		policy_same "$c: mounted" \
		    "$(get_prop mounted $TESTPOOL/$c)" no
	fi
done
policy_dry_run
policy_run together $((4 * CD_BLOCKS)) 0 0
for c in $REFUSED; do
	policy_same "$c: a after the run together" \
	    "$(policy_blocks $TESTPOOL/$c a)" "${ba[$c]}"
	policy_same "$c: b after the run together" \
	    "$(policy_blocks $TESTPOOL/$c b)" "${bb[$c]}"
done

for c in $CASES; do
	ds=$TESTPOOL/$c
	if [[ $c == o* ]]; then
		log_must zfs mount $ds
	fi
	policy_same "$c: content of a" \
	    "$(xxh128digest /$ds/a)" "${ha[$c]}"
	policy_same "$c: content of b" \
	    "$(xxh128digest /$ds/b)" "${hb[$c]}"
done
clonedup_leakcheck

log_pass "immutable and over-quota files are refused, owned or not"
