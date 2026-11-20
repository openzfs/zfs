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
# Copyright (c) 2026 Miguel Cervera
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Indirect writes of one block, synced while the txg that holds them is
#	syncing, share a single write of the block.
#
#	When the txg of a write is already syncing, dmu_sync() can't use the
#	dirty record, and writes the block again for the log
#	(dmu_sync_late_arrival()).  A block written in several chunks used to
#	be written once per chunk, in parallel, each with its own buffers.
#	The log records of the chunks must instead share one block pointer.
#
# STRATEGY:
#	1. Stop txgs from syncing on their own
#	2. Write NREC blocks, each as four 32K writes (one txg), with
#	   logbias=throughput so every write is logged indirect
#	3. Start syncing the txg with writes to the data vdev delayed, wait
#	   until it is syncing, then fsync from several processes at once and
#	   save the vdevs.  Retry if that txg finished syncing, or wrote an
#	   uberblock to a vdev, before the copies were done.  The delay keeps the first late write of each
#	   block in flight while the get_data calls for the block's other
#	   three records run, so the counts in step 5 hold.
#	4. Restore the saved vdevs, which hold the log but not the writes, as
#	   after a crash
#	5. Check that the log has one indirect TX_WRITE record for each write,
#	   and that each of the NREC blocks is written once, shared by four
#	   log records
#	6. Import the saved vdevs, replaying the log, and check the file
#

verify_runnable "global"

typeset -i syncpid=0
typeset -a fpids
typeset -i NREC=64
set -A vdevs $VDIR/big $VDIR/e

function cleanup_fs
{
	zinject -c all
	# try_fsync may have failed with background processes running.
	((syncpid != 0)) && wait $syncpid
	[[ -n ${fpids[*]} ]] && wait "${fpids[@]}"
	restore_tunable TXG_TIMEOUT
	restore_tunable TXG_HISTORY
	rm -f $VDIR2/big.save $VDIR2/e.save
	cleanup
}

# Print the number of a txg that is syncing, if there is one.
function txg_syncing
{
	kstat_pool $TESTPOOL txgs | awk '$3 == "S" { print $1; exit }'
}

# Succeed if txg $1 is still syncing.  The state of a txg only advances, so
# this means it was syncing throughout.  (Others may sync after it: zpool
# sync waits for the txgs that follow, too.)
function txg_is_syncing # txg
{
	kstat_pool $TESTPOOL txgs | \
	    awk -v t=$1 '$1 == t && $3 == "S" { found = 1 } END { exit !found }'
}

# Succeed if file $1 holds an uberblock of txg $2 or later.
function has_uberblock # file txg
{
	zdb -lu $1 | awk -v t=$2 \
	    '$1 == "txg" && $2 == "=" && $3 >= t { found = 1 } END { exit !found }'
}

# Write the file, fsync while its txg is syncing, and save the vdevs while
# it still is.  Return 1 if the txg finished syncing before that was done.
function try_fsync
{
	typeset f
	typeset -i i failed=0
	typeset txg=""

	dd if=$TESTDIR/pat of=$file bs=32k count=$((4 * NREC)) \
	    conv=notrunc 2>/dev/null || log_fail "write failed"

	log_must zinject -d ${vdevs[0]} -D 500:64 -T write $TESTPOOL
	zpool sync $TESTPOOL &
	syncpid=$!
	for ((i = 0; i < 100; i++)); do
		txg=$(txg_syncing)
		[[ -n $txg ]] && break
		sleep 0.05
	done
	if [[ -z $txg ]]; then
		log_note "the txg did not start syncing"
		return 1
	fi
	# Several fsyncs at once, so that lwbs are filled concurrently.
	for ((i = 0; i < 4; i++)); do
		dd if=/dev/null of=$file conv=notrunc,fsync 2>/dev/null &
		fpids[i]=$!
	done
	for ((i = 0; i < 4; i++)); do
		wait ${fpids[i]} || ((failed++))
	done
	((failed == 0)) || log_fail "fsync failed"
	if ! txg_is_syncing $txg; then
		log_note "txg $txg finished syncing during the fsync"
		return 1
	fi

	# Save the vdevs.  The delay holds back only the completion of a
	# write, not the write: the blocks and the uberblock of the txg reach
	# the file when issued, and the txg stays syncing until the delayed
	# uberblock write completes.  The image holds the log but not the
	# writes only if neither saved file has an uberblock of the txg, so
	# check the saved files, as well as that the txg is still syncing.
	log_must cp ${vdevs[0]} $VDIR2/big.save
	log_must cp ${vdevs[1]} $VDIR2/e.save
	if ! txg_is_syncing $txg; then
		log_note "txg $txg finished syncing while saving the vdevs"
		return 1
	fi
	for f in $VDIR2/big.save $VDIR2/e.save; do
		if has_uberblock $f $txg; then
			log_note "txg $txg is in the saved $f"
			return 1
		fi
	done
	return 0
}

log_assert "Indirect writes of one block synced while its txg is syncing" \
    "share one block pointer."
log_onexit cleanup_fs
log_must setup
log_must save_tunable TXG_TIMEOUT
log_must save_tunable TXG_HISTORY
log_must set_tunable32 TXG_HISTORY 100

typeset file=/$TESTPOOL/$TESTFS/file

log_must truncate -s 1g ${vdevs[0]}
log_must zpool create $TESTPOOL ${vdevs[0]} log ${vdevs[1]}
log_must zfs create -o compression=off -o recordsize=128k \
    -o logbias=throughput $TESTPOOL/$TESTFS

log_must mkdir -p $TESTDIR
log_must dd if=/dev/urandom of=$TESTDIR/pat bs=128k count=$NREC
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
# 2, 3. The writes, then an fsync during the txg's sync.
#
typeset -i attempt ok=0
for ((attempt = 1; attempt <= 5; attempt++)); do
	if try_fsync; then
		ok=1
		break
	fi
	# Let the txg finish.  Log blocks of the late arrival writes stay
	# until the next txg syncs (they wait for the txg after it), so sync
	# again, to keep them out of the next attempt's image.
	log_must zinject -c all
	wait $syncpid
	log_must sync_pool $TESTPOOL
done
((ok == 1)) || log_fail "the fsync and the copies never ran while the txg was syncing"

#
# 4. The vdevs were saved in try_fsync; let the pool go.
#
log_must zinject -c all
wait $syncpid
log_must zpool export $TESTPOOL
log_must cp $VDIR2/big.save ${vdevs[0]}
log_must cp $VDIR2/e.save ${vdevs[1]}

#
# 5. The log.
#
typeset recs
recs=$(zdb -e -p $VDIR -ivvvvv $TESTPOOL/$TESTFS) || \
    log_fail "zdb -i failed"
echo "$recs" | grep -E "TX_WRITE2? +len" | head -n 8
typeset -i nw=$(echo "$recs" | grep -c "has blkptr")
typeset -i nw2=$(echo "$recs" | grep -cE "TX_WRITE2 +len")
typeset dvas
dvas=$(echo "$recs" | grep "ZFS plain file" | \
    grep -oE "DVA\[0\]=<[0-9]+:[0-9a-f]+:[0-9a-f]+>")
typeset -i ndistinct=$(echo "$dvas" | sort -u | wc -l)
typeset -i nshared=$(echo "$dvas" | sort | uniq -d | wc -l)
log_note "log: $nw indirect TX_WRITEs, $ndistinct distinct blocks," \
    "$nshared shared blocks, $nw2 TX_WRITE2s"
((nw == 4 * NREC)) || log_fail "expected $((4 * NREC)) indirect TX_WRITEs"
((nw2 == 0)) || log_fail "expected no TX_WRITE2s"
((nshared == NREC)) || log_fail "expected $NREC shared blocks"
((ndistinct == NREC)) || log_fail "expected $NREC distinct blocks"

#
# 6. Replay from the shared blocks.
#
log_must zpool import -d $VDIR $TESTPOOL
log_must cmp $file $TESTDIR/pat
log_must zpool export $TESTPOOL

log_pass "Indirect writes of one block synced while its txg is syncing" \
    "share one block pointer."
