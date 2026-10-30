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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	A resilver must copy a dedup block that gains a reference in
#	a file the resilver has passed, after the older reference is
#	removed, when the pool is exported before the resilver reads
#	the block.
#
# STRATEGY:
#	1. Create a pool on one disk and many empty files with
#	   dedup=on.  The one with the lowest object number is D and
#	   the one with the highest is S.  Write S.  Its block is
#	   unique in the DDT.
#	2. Export and import to empty the ARC, delay reads of the
#	   disk, take a scan checkpoint every second and attach a
#	   second disk.  That starts a full resilver.
#	3. Freeze the resilver once a checkpoint follows a pass that
#	   started past D.  The scan state on disk must show a full
#	   resilver and a bookmark between D and S, or the run
#	   missed the window and is repeated.
#	4. Copy S onto D, which moves the block to the duplicate
#	   class, and remove S.  The copy must raise dedupsaved.
#	   After the removal dedupsaved must be 0 and dedupused must
#	   not.  The saved bookmark must not have moved, and the
#	   scan flags on disk must carry DSF_QUEUED_UNWALKED.
#	5. export exports and imports the pool, then releases the
#	   resilver.  dbgmsg must show that the import restarted the
#	   scan for that flag.  noexport releases the resilver
#	   without the export, and dbgmsg must show that the flag
#	   was cleared once the hook's read was done.
#	6. Detach the first disk and verify D cold.  A failed read
#	   or a failed flag check fails the test after the last arm.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	restore_tunable SCAN_SUSPEND_PROGRESS
	restore_tunable SCAN_CHECKPOINT_INTVAL
	[[ -n $pool ]] && destroy_pool $pool
	restore_tunable NO_SCRUB_PREFETCH
	restore_tunable TXG_TIMEOUT
	restore_tunable DBGMSG_ENABLE
	rm -rf $workdir
}

function destroy_arm
{
	destroy_pool $pool
	pool=
	log_must rm $workdir/disk-0 $workdir/disk-1
}

#
# Run one arm on a new pool.  Return 1 if it missed the window.
#
function run_arm # export|noexport try
{
	typeset arm=$1 try=$2
	typeset mntpnt fdir os d dobj s sobj
	typeset -i npass issued
	typeset -a ext sp sp2

	pool="$TESTPOOL1-${workdir##*.}-$arm-$try"
	log_must truncate -s 512M $workdir/disk-0 $workdir/disk-1
	log_must zpool create -o feature@block_cloning=disabled \
	    $pool $workdir/disk-0
	log_must zfs create -o compression=off -o recordsize=128k \
	    -o dedup=on $pool/$TESTFS
	mntpnt=$(get_prop mountpoint $pool/$TESTFS)
	fdir=$mntpnt/files
	log_must mkdir $fdir
	log_must mkfiles $fdir/f $RB_NFILES
	set -A ext -- $(ls -i $fdir | sort -n | sed -n '1p;$p')
	dobj=${ext[0]}
	d=$fdir/${ext[1]}
	sobj=${ext[2]}
	s=$fdir/${ext[3]}
	os=$(get_prop objsetid $pool/$TESTFS)
	log_note "$arm: D is object $dobj, S is $sobj in objset $os"
	log_must dd if=$workdir/expected of=$s bs=128k
	sync_pool $pool
	(( $(get_pool_prop dedupsaved $pool) == 0 )) ||
	    log_fail "$arm: S's block is not unique"

	log_must_busy zpool export $pool
	log_must zpool import -d $workdir $pool
	log_must zinject -d $workdir/disk-0 -D $RB_DELAY_MS:1 \
	    -T read $pool
	log_must zpool attach $pool $workdir/disk-0 $workdir/disk-1
	if ! freeze_past_checkpoint $pool $os $dobj; then
		log_note "$arm: try $try missed the window," \
		    "no checkpoint followed a pass past D in time"
		log_must zinject -c all
		destroy_arm
		return 1
	fi
	sync_pool $pool
	set -A sp -- $(scan_phys $pool)
	npass=$(scan_passes $pool | wc -l)
	(( ${#sp[@]} == 24 )) ||
	    log_fail "$arm: zdb shows no scan state"
	(( sp[14] == 1 )) || log_fail "$arm: the resilver is not full"
	log_note "$arm: saved bookmark ${sp[19]}/${sp[20]}"
	if (( sp[19] != os || sp[20] <= dobj ||
	    sp[20] >= sobj )); then
		log_note "$arm: try $try missed the window"
		log_must zinject -c all
		destroy_arm
		return 1
	fi

	log_must dd if=$s of=$d bs=128k
	sync_pool $pool
	(( $(get_pool_prop dedupsaved $pool) > 0 )) ||
	    log_fail "$arm: D does not share S's block"
	log_must rm $s
	sync_pool $pool
	sync_pool $pool
	(( $(get_pool_prop dedupsaved $pool) == 0 )) ||
	    log_fail "$arm: S still shares D's block"
	(( $(get_pool_prop dedupused $pool) > 0 )) ||
	    log_fail "$arm: D's block left the DDT"

	log_must zinject -c all
	set -A sp2 -- $(scan_phys $pool)
	(( ${#sp2[@]} == 24 )) ||
	    log_fail "$arm: zdb shows no scan state"
	[[ ${sp2[19]}/${sp2[20]} == ${sp[19]}/${sp[20]} ]] &&
	    (( $(scan_passes $pool | wc -l) == npass )) ||
	    log_fail "$arm: the resilver moved while frozen"
	if (( !(sp2[23] & RB_UNWALKED) )); then
		log_note "$arm: the scan flag is not on disk"
		wrong="$wrong $arm:set"
	fi
	[[ $arm == export ]] && log_must_busy zpool export $pool
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	if [[ $arm == export ]]; then
		log_must zpool import -d $workdir $pool
	fi
	log_must zpool wait -t resilver $pool
	if [[ $arm == export ]] &&
	    ! scan_restarted_unwalked $pool; then
		log_note "export: the import did not restart the scan"
		wrong="$wrong export:restart"
	fi
	if [[ $arm == noexport ]] &&
	    ! scan_cleared_unwalked $pool; then
		log_note "noexport: the scan flag was not cleared"
		wrong="$wrong noexport:clear"
	fi
	issued=$(kstat dbgmsg |
	    grep -c "scan issued [0-9]* blocks for $pool (")
	(( issued > 0 )) ||
	    log_fail "$arm: the scan did not issue from sorted queues"

	log_must zpool detach $pool $workdir/disk-0
	log_must_busy zpool export $pool
	log_must zpool import -d $workdir $pool
	if ! cmp $workdir/expected $d; then
		log_note "$arm: $d does not read back"
		lost="$lost $arm"
	fi
	destroy_arm
	return 0
}

log_assert "A resilver copies a dedup block referenced behind it" \
    "across an export"

workdir=$(mktemp -d $TEST_BASE_DIR/dedup_resilver_export.XXXXXX) ||
    log_fail "cannot create test directory"
pool=
lost=
wrong=
log_onexit cleanup

log_must save_tunable DBGMSG_ENABLE
log_must save_tunable TXG_TIMEOUT
log_must save_tunable NO_SCRUB_PREFETCH
log_must save_tunable SCAN_SUSPEND_PROGRESS
log_must save_tunable SCAN_CHECKPOINT_INTVAL
log_must set_tunable32 DBGMSG_ENABLE 1
log_must set_tunable32 TXG_TIMEOUT 1
log_must set_tunable32 NO_SCRUB_PREFETCH 1
log_must set_tunable32 SCAN_CHECKPOINT_INTVAL 1
log_must file_write -o create -f $workdir/expected -b 131072 \
    -c 1 -d 65

typeset -i n
for arm in noexport export; do
	for (( n = 1; n <= RB_TRIES; n++ )); do
		run_arm $arm $n && break
	done
	(( n <= RB_TRIES )) ||
	    log_fail "$arm: did not hit the window in $RB_TRIES tries"
	log_note "$arm: needed $n of $RB_TRIES tries"
done
[[ -z $lost && -z $wrong ]] || log_fail "the block did not read" \
    "back in:${lost:- none}, the flag was wrong in:${wrong:- none}"

log_pass "A resilver copied a dedup block referenced behind it" \
    "across an export"
