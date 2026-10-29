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
#	A verify read never reads a block that was freed after its
#	pair was queued.  The apply reads both blocks of a pair when
#	the batch flushes.  A block freed in between, whose space is
#	then written over, would fail its checksum on healthy disks:
#	CKSUM errors, an ereport and an entry in the error log.  The
#	source is freed by a rewrite, by a remove, and by a rewrite
#	after its dataset is mounted, and a dry run's destination by
#	a rewrite.
#
# STRATEGY:
#	1. Build a raidz1 pool on three files.
#	2. In each round write two files of the same size, finish a
#	   run, and copy them.  The next run's match walk then finds
#	   the files as the sources.  Otherwise a copy whose BRT
#	   range holds a clone from an earlier round can outrank its
#	   file.  The pairs of the one with the lower object number
#	   are queued first, and the other one's pairs keep the batch
#	   open.  One worker sleeps before it queues each pair.
#	3. Once the first pair is queued, free its block, sync four
#	   txgs, and initialize the pool, which writes a pattern over
#	   all free space.
#	4. Check that no verify read was issued before the pattern
#	   was written, and that the rewrite waited for the queued
#	   pair, or for the mount, that the apply let go first.
#	5. After each run check for READ, WRITE and CKSUM errors,
#	   data errors, checksum, data and io ereports and clonedup
#	   errors, that every pair was counted, and every file.
#	6. A scrub repairs nothing.
#

verify_runnable "global"

typeset -r VF_DIR=$TEST_BASE_DIR/clonedup_vf
typeset -ri VF_BLOCKS=32
typeset -ri VF_DELAY=500
typeset vf_pid=""

function cleanup
{
	[[ -n $vf_pid ]] && kill $vf_pid 2>/dev/null
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_THREADS
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	clonedup_cleanup
	rm -f $VF_DIR/v1 $VF_DIR/v2 $VF_DIR/v3 $VF_DIR/want \
	    $VF_DIR/new
	[[ -d $VF_DIR ]] && rmdir $VF_DIR
}

# One scan counter, from the kernel rather than from disk.
function vf_live # key
{
	zpool status -j --json-int $TESTPOOL |
	    jq -r --arg p $TESTPOOL ".pools[\$p].scan_stats.$1"
}

# Wait up to two minutes for kstat $1 to pass $2.
function vf_kstat_above # name value
{
	typeset -i i

	for ((i = 0; i < 1200; i++)); do
		(( $(clonedup_kstat $1) > $2 )) && return 0
		sleep 0.1
	done
	log_fail "kstat $1 stayed at $2"
}

#
# Fail on a vdev error count, a logged data error or an error
# ereport.
#
function vf_clean # what
{
	typeset out ev v

	out=$(zpool status -p $TESTPOOL | awk '
	    /NAME +STATE +READ +WRITE +CKSUM/ { t = 1; next }
	    t && NF == 0 { exit }
	    t && NF >= 5 { print $1 ":" $3 "/" $4 "/" $5 }')
	log_note "$1: read/write/cksum:" $out
	for v in $out; do
		[[ ${v#*:} == 0/0/0 ]] || log_fail "$1: errors on $v"
	done
	out=$(zpool status -v $TESTPOOL | grep '^errors:')
	log_note "$1: $out"
	[[ $out == "errors: No known data errors" ]] ||
	    log_fail "$1: $(zpool status -v $TESTPOOL)"
	ev=$(zpool events -H $TESTPOOL | awk '{ print $NF }' |
	    grep -E '^ereport\.fs\.zfs\.(checksum|data|io)$')
	log_note "$1: error ereports:" $ev
	[[ -z $ev ]] || log_fail "$1: $(zpool events -v $TESTPOOL)"
}

#
# One round.  $1 names it, $2 and $3 are the datasets of the files
# and of their copies, $4 is rewrite, remove or mount, and $5 is
# -n for a dry run, whose rewrite goes to the copy.
#
function vf_round # name srcds dstds how [-n]
{
	typeset name=$1 sds=$TESTPOOL/$2 dds=$TESTPOOL/$3 how=$4
	typeset dry=$5 s p victim held=no
	typeset dwant=$VF_DIR/want swant=$VF_DIR/new
	typeset -i o1 o2 m0 v0 v1 y0 y1 rc applied stale errs i

	datasetexists $sds || log_must zfs create $sds
	datasetexists $dds || log_must zfs create $dds
	clonedup_write /$sds/f1 $VF_BLOCKS
	clonedup_write /$sds/f2 $VF_BLOCKS
	clonedup_sync
	clonedup_run
	o1=$(ls -i /$sds/f1 | awk '{ print $1 }')
	o2=$(ls -i /$sds/f2 | awk '{ print $1 }')
	if (( o1 < o2 )); then
		s=f1 p=f2
	else
		s=f2 p=f1
	fi
	log_note "$name: racing $sds/$s, padding $sds/$p"
	clonedup_dup /$sds/$s /$dds/d
	clonedup_dup /$sds/$p /$dds/q
	clonedup_sync
	log_must dd if=/$sds/$s of=$VF_DIR/want bs=$CD_BS status=none
	clonedup_write $VF_DIR/new $VF_BLOCKS
	[[ $how == mount ]] && log_must zfs unmount $sds
	log_must zpool events -c

	m0=$(clonedup_kstat dst_mounted)
	v0=$(clonedup_kstat verify_reads)
	y0=$(clonedup_kstat yields)
	log_must zpool clonedup $dry $TESTPOOL
	clonedup_wait_applying
	vf_kstat_above dst_mounted $m0
	log_must sleep $((VF_DELAY * 4 / 1000.0))
	v1=$(clonedup_kstat verify_reads)
	log_note "$name: first pair queued, $((v1 - v0)) verify reads"
	log_must [ $v1 -eq $v0 ]

	if [[ $how == mount ]]; then
		log_must zfs mount $sds
		y1=$(clonedup_kstat yields)
		log_note "$name: yields for the mount: $((y1 - y0))"
	fi
	if [[ $how == remove ]]; then
		log_must rm /$sds/$s
	else
		victim=/$sds/$s
		[[ -n $dry ]] && victim=/$dds/d
		dd if=$VF_DIR/new of=$victim bs=$CD_BS conv=notrunc \
		    status=none &
		vf_pid=$!
	fi
	for ((i = 0; i < 4; i++)); do
		log_must zpool sync $TESTPOOL
	done
	log_must zpool initialize -w $TESTPOOL
	v1=$(clonedup_kstat verify_reads)
	log_note "$name: after the initialize, $((v1 - v0)) verify" \
	    "reads"
	[[ -n $vf_pid ]] && kill -0 $vf_pid 2>/dev/null && held=yes
	log_note "$name: rewrite still waiting: $held"

	log_must zpool wait -t clonedup $TESTPOOL
	if [[ -n $vf_pid ]]; then
		wait $vf_pid
		rc=$?
		vf_pid=""
		log_note "$name: rewrite exited $rc"
		log_must [ $rc -eq 0 ]
	fi

	vf_clean $name
	typeset skips="stale sources"
	if [[ -n $dry ]]; then
		skips=skipped
		applied=$(vf_live clonedup_applied)
		stale=$(vf_live clonedup_skipped)
		errs=$(vf_live errors)
	else
		applied=$(vf_live clonedup_applied)
		stale=$(vf_live clonedup_skipped_stale)
		errs=$(vf_live errors)
	fi
	typeset skipped=$(vf_live clonedup_skipped)
	clonedup_assert_counts "$skipped"
	log_note "$name: applied $applied, $skips $stale," \
	    "errors $errs, skipped in all $skipped"
	log_must [ $errs -eq 0 ]
	log_must [ $((applied + skipped)) -eq $((2 * VF_BLOCKS)) ]
	if [[ $how == mount ]]; then
		log_must [ $((y1 - y0)) -gt 0 ]
		log_must [ $applied -ge $VF_BLOCKS ]
	else
		log_must [ $v1 -eq $v0 ]
		log_must [ $applied -eq $((2 * VF_BLOCKS)) ]
	fi
	[[ $how == rewrite ]] && log_must [ $held = yes ]

	[[ -n $dry ]] && dwant=$VF_DIR/new swant=$VF_DIR/want
	log_must cmp /$dds/d $dwant
	[[ $how == remove ]] || log_must cmp /$sds/$s $swant
	log_must cmp /$dds/q /$sds/$p
	log_must zpool initialize -u $TESTPOOL
}

log_assert "a verify read never reads a freed block"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_THREADS
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG

log_must mkdir $VF_DIR
for v in v1 v2 v3; do
	log_must truncate -s $MINVDEVSIZE $VF_DIR/$v
done
log_must zpool create -f -o feature@clonedup=enabled \
    -O recordsize=128k -O compression=off $TESTPOOL \
    raidz1 $VF_DIR/v1 $VF_DIR/v2 $VF_DIR/v3

log_must set_tunable32 CLONEDUP_APPLY_THREADS 1
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 1024
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY $VF_DELAY

vf_round rewrite a1 b1 rewrite
vf_round remove a2 a2 remove
vf_round mount a3 b3 mount
vf_round dry a4 b4 rewrite -n

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 0
log_must zpool scrub -w $TESTPOOL
typeset scan=$(zpool status $TESTPOOL | grep 'scan:')
log_note "after the scrub: $scan"
[[ $scan == *"repaired 0B"*"with 0 errors"* ]] || log_fail "$scan"
vf_clean scrub
clonedup_leakcheck

log_pass "a verify read never reads a freed block"
