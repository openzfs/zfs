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
# Copyright 2026 Oxide Computer Company
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Replay of a log that ends early must apply a prefix of the logged
#	writes, even when an indirect write's block also holds later writes,
#	and replay of the whole log must restore every write, including those
#	whose records share another record's block.
#
#	An indirect (WR_INDIRECT) TX_WRITE points to a copy of the whole
#	block, taken when the log block is written, which also holds later
#	writes to other parts of the block.  Replay used to write the whole
#	block, which brought back those later writes even when their own
#	log records were lost.  Such a later write's record holds the same
#	block pointer as the earlier record, because dmu_sync() finds its
#	block already written for that record.  (Older software logged it as
#	a TX_WRITE2, with no block pointer.)
#
# STRATEGY:
#	1. Stop txgs from syncing on their own
#	2. For each of NREC records, write its first 64K (W1); then for each
#	   record, write 32K at 64K (W2); then fsync.  logbias=throughput
#	   logs every write indirect, and each W2 finds its block already
#	   being written for W1, so its record shares W1's block pointer.
#	3. Save the vdevs, which then hold the log but not the writes, as
#	   after a crash
#	4. Import without mounting, which claims the log but doesn't replay
#	   it, and check that zdb -b counts each shared block once
#	5. For the whole log, and for each log block after the first:
#	   restore the saved vdevs, zero that log block, import (replaying
#	   the log up to it), and check that the writes present are a prefix
#	   of W1[0..NREC-1], W2[0..NREC-1].  The whole log must restore every
#	   write.  The first log block ends among the W1 records, so with
#	   whole-block replay the cut at the second block brings back W2
#	   writes whose records are lost.
#

verify_runnable "global"

function cleanup_fs
{
	restore_tunable TXG_TIMEOUT
	cleanup
	rm -f $VDIR2/big.save $VDIR2/e.save
}

# Print new, old or torn for the range bs * [skip, skip + 1) of the file.
function range_state # bs skip pattern zeros
{
	typeset bs=$1 skip=$2 pat=$3 zeros=$4

	dd if=$file bs=$bs skip=$skip count=1 2>/dev/null > $TESTDIR/range
	if cmp -s $TESTDIR/range $pat; then
		echo new
	elif cmp -s $TESTDIR/range $zeros; then
		echo old
	else
		echo torn
	fi
}

# Check that the writes present are a prefix of W1[], W2[]; set replayed to
# how many there are.
function check_prefix # name
{
	typeset name=$1
	typeset -i i n=0 bad=0
	typeset st op old=""

	for ((i = 0; i < 2 * NREC; i++)); do
		if ((i < NREC)); then
			st=$(range_state 64k $((2 * i)) $TESTDIR/w1 $TESTDIR/z1)
			op="W1[$i]"
		else
			st=$(range_state 32k $((4 * (i - NREC) + 2)) \
			    $TESTDIR/w2 $TESTDIR/z2)
			op="W2[$((i - NREC))]"
		fi
		case $st in
		new)
			if [[ -n $old ]]; then
				log_note "$name: $op present, $old missing"
				((bad++))
			else
				((n++))
			fi
			;;
		old)
			[[ -z $old ]] && old=$op
			;;
		*)
			log_note "$name: $op neither old nor new"
			((bad++))
			;;
		esac
	done
	log_note "$name: $n of $((2 * NREC)) writes replayed"
	((bad == 0)) || log_fail "$name: replay did not apply a prefix"
	replayed=$n
}

log_assert "Replay of a truncated log applies a prefix of the indirect writes."
log_onexit cleanup_fs
log_must setup
log_must save_tunable TXG_TIMEOUT

typeset file=/$TESTPOOL/$TESTFS/file
typeset -i NREC=64
typeset -i replayed
set -A vdevs $VDIR/big $VDIR/e

# A small pool makes the writes' space reservations wait for txgs to sync.
log_must truncate -s 1g ${vdevs[0]}
log_must zpool create $TESTPOOL ${vdevs[0]} log ${vdevs[1]}
log_must zfs create -o compression=off -o recordsize=128k \
    -o logbias=throughput $TESTPOOL/$TESTFS

log_must mkdir -p $TESTDIR
log_must dd if=/dev/urandom of=$TESTDIR/w1 bs=64k count=1
log_must dd if=/dev/urandom of=$TESTDIR/w2 bs=32k count=1
log_must dd if=/dev/zero of=$TESTDIR/z1 bs=64k count=1
log_must dd if=/dev/zero of=$TESTDIR/z2 bs=32k count=1
log_must dd if=/dev/zero of=$file bs=128k count=$NREC

# Create the ZIL header now, so that the fsync below doesn't sync a txg.
log_must dd if=/dev/zero of=/$TESTPOOL/$TESTFS/sync \
    oflag=sync bs=1 count=1
log_must sync_pool $TESTPOOL

#
# 1. Stop txgs from syncing on their own.  The sync thread picks up the new
#    timeout after the next txg.
#
log_must set_tunable32 TXG_TIMEOUT 3600
log_must sync_pool $TESTPOOL

#
# 2. The writes, then an fsync, which writes the log.
#
for ((i = 0; i < NREC; i++)); do
	dd if=$TESTDIR/w1 of=$file bs=64k count=1 seek=$((2 * i)) \
	    conv=notrunc 2>/dev/null || log_fail "W1[$i] failed"
done
for ((i = 0; i < NREC; i++)); do
	dd if=$TESTDIR/w2 of=$file bs=32k count=1 seek=$((4 * i + 2)) \
	    conv=notrunc 2>/dev/null || log_fail "W2[$i] failed"
done
log_must dd if=/dev/null of=$file conv=notrunc,fsync

#
# 3. Save the vdevs, then let the pool go.
#
log_must cp ${vdevs[0]} $VDIR2/big.save
log_must cp ${vdevs[1]} $VDIR2/e.save
log_must restore_tunable TXG_TIMEOUT
log_must zpool export $TESTPOOL
log_must cp $VDIR2/big.save ${vdevs[0]}
log_must cp $VDIR2/e.save ${vdevs[1]}

typeset recs=$(zdb -e -p $VDIR -ivvvvv $TESTPOOL/$TESTFS)
echo "$recs" | grep -E "Block seqno|TX_WRITE2? +len"
set -A blks $(echo "$recs" | awk '/Block seqno/ {
	if (match($0, /DVA\[0\]=<[0-9]+:[0-9a-f]+:[0-9a-f]+>/))
		print substr($0, RSTART + 8, RLENGTH - 9) }')
typeset -i nw=$(echo "$recs" | grep -c "has blkptr")
typeset -i nw2=$(echo "$recs" | grep -cE "TX_WRITE2 +len")
typeset -i nshared=$(echo "$recs" | grep "ZFS plain file" | \
    grep -oE "DVA\[0\]=<[0-9]+:[0-9a-f]+:[0-9a-f]+>" | sort | uniq -d | wc -l)
log_note "log: ${#blks[@]} blocks, $nw indirect TX_WRITEs," \
    "$nshared shared blocks, $nw2 TX_WRITE2s"
((nw == 2 * NREC)) || log_fail "expected $((2 * NREC)) indirect TX_WRITEs"
((nshared == NREC)) || log_fail "expected $NREC shared blocks"
((nw2 == 0)) || log_fail "expected no TX_WRITE2s"
((${#blks[@]} >= 2)) || log_fail "expected a log of several blocks"

#
# 4. zdb -b with the log claimed but not replayed.
#
log_must zpool import -N -d $VDIR $TESTPOOL
typeset zdbout
zdbout=$(zdb -b $TESTPOOL 2>&1) || log_fail "zdb -b failed: $zdbout"
# zdb -b doesn't fail on a leak, so check for one.
[[ $zdbout == *"No leaks"* ]] || log_fail "zdb -b: $zdbout"
log_must zpool export $TESTPOOL

#
# 5. Replay the whole log, then the log cut at each block after the first.
#
typeset -i k
for ((k = 0; k < ${#blks[@]}; k++)); do
	log_must cp $VDIR2/big.save ${vdevs[0]}
	log_must cp $VDIR2/e.save ${vdevs[1]}
	if ((k == 0)); then
		name="whole log"
	else
		# DVA vdev:offset:asize; the data starts after the 4M of labels.
		typeset vd=${blks[$k]%%:*}
		typeset rest=${blks[$k]#*:}
		typeset -i off=$((16#${rest%%:*} + 4194304))
		typeset -i asize=$((16#${rest#*:}))
		name="log cut at block $((k + 1)) of ${#blks[@]}"
		log_must dd if=/dev/zero of=${vdevs[$vd]} bs=512 \
		    seek=$((off / 512)) count=$((asize / 512)) conv=notrunc
	fi
	log_must zpool import -d $VDIR $TESTPOOL
	check_prefix "$name"
	if ((k == 0)); then
		((replayed == 2 * NREC)) || \
		    log_fail "whole log: $replayed writes replayed"
	elif ((k == 1 && replayed >= NREC)); then
		log_fail "$name: the first log block holds every W1"
	fi
	log_must zpool export $TESTPOOL
done

log_pass "Replay of a truncated log applies a prefix of the indirect writes."
