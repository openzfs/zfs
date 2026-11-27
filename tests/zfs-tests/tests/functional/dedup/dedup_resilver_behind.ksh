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

#
# DESCRIPTION:
#	A resilver must copy a dedup block that gains a reference in a
#	file the resilver has already passed, after every other
#	reference to the block is removed.
#
# STRATEGY:
#	1. Offline the second side of a mirror and create many empty
#	   files with dedup=on.  The one with the lowest object number
#	   is D, the next to highest is S and the highest is S2.
#	   Write S and copy it to S2, so that they share one block in
#	   the duplicate class of the DDT.
#	2. Export and import to empty the ARC, delay reads of the
#	   first side and online the second.  The resilver is
#	   incremental, so its DDT phase skips duplicate blocks and
#	   only the walk can find this one.
#	3. behind: freeze the resilver once a pass starts past D, copy
#	   S onto D with read and write calls, remove S and S2 and let
#	   the resilver finish.  The copy must raise dedupsaved, and
#	   after the removal dedupsaved must be 0 while dedupused is
#	   not, and bcloneused must stay 0.  The first pass after the
#	   freeze must start between D and S, or the run missed the
#	   window and is repeated.
#	4. Detach the first side and verify D cold.
#	5. ahead freezes the resilver before it starts, so D is still
#	   ahead of it.  nocopy freezes it between D and S and copies
#	   nothing, then verifies S.  Both must read back intact.
#	6. Run behind again on a pool without fast_dedup, whose
#	   DDT has no log.
#	7. Run ahead and behind again with block cloning, cloning S
#	   onto D instead of copying it, so that ddt_addref() takes
#	   the new reference.  FICLONE cannot fall back to a copy.
#	   FreeBSD's copy_file_range can, which would hide the clone
#	   path, so FreeBSD skips this.
#

verify_runnable "global"

#
# Without scan prefetch a resilver pass reads one dnode block, 32
# files, every DELAY_MS, so the walk over NFILES files takes about
# 10s.  With prefetch, the next pass would walk past S from the cache.
#
NFILES=16000
DELAY_MS=20
TRIES=3

#
# Print "txg objset object" for each scan pass of pool $1, or
# "txg ddt 0" for a pass that starts in the DDT.
#
function scan_passes # pool
{
	kstat dbgmsg | awk -v re="doing scan sync for $1 txg " '
		(i = index($0, re)) == 0 { next }
		{
			s = substr($0, i + length(re))
			split(s, f, "[;/= ]+")
			if (f[2] == "ddt")
				print f[1], "ddt", 0
			else
				print f[1], f[3], f[4]
		}'
}

#
# Freeze the resilver of pool $1 once a pass starts in objset $2 past
# object $3.  Return 1 if the resilver ends first.
#
function freeze_past # pool objset object
{
	typeset -a bm
	typeset -i i

	for (( i = 0; i < 1200; i++ )); do
		set -A bm -- $(scan_passes $1 | tail -1)
		if [[ ${bm[1]} == "$2" ]] && (( bm[2] > $3 )); then
			log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
			return 0
		fi
		is_pool_resilvered $1 && return 1
		sleep 0.1
	done
	return 1
}

function cleanup
{
	zinject -c all >/dev/null 2>&1
	restore_tunable SCAN_SUSPEND_PROGRESS
	[[ -n $pool ]] && destroy_pool $pool
	tunable_exists BCLONE_ENABLED &&
	    restore_tunable BCLONE_ENABLED
	restore_tunable NO_SCRUB_PREFETCH
	restore_tunable SCAN_LEGACY
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
# Put S's block into D by copying or by cloning.
#
function share # dd|clone s d
{
	if [[ $1 == dd ]]; then
		log_must dd if=$2 of=$3 bs=128k
	else
		log_must clonefile -c $2 $3
	fi
}

#
# Run one arm on a new pool.  Return 1 if it missed the window.
#
function run_arm # fdt|legacy dd|clone behind|ahead|nocopy try
{
	typeset fmt=$1 how=$2 arm=$3 try=$4
	typeset tag="$fmt $how $arm"
	typeset fdt=enabled bclone=disabled
	typeset mntpnt fdir os d dobj s sobj s2 txg
	typeset -i saved
	typeset -a ext bm

	[[ $fmt == legacy ]] && fdt=disabled
	[[ $how == clone ]] && bclone=enabled
	pool="$TESTPOOL1-${workdir##*.}-$fmt-$how-$arm-$try"
	log_must truncate -s 512M $workdir/disk-0 $workdir/disk-1
	log_must zpool create -o feature@block_cloning=$bclone \
	    -o feature@fast_dedup=$fdt $pool \
	    mirror $workdir/disk-0 $workdir/disk-1
	log_must zfs create -o compression=off -o recordsize=128k \
	    -o dedup=on -o xattr=sa $pool/$TESTFS
	mntpnt=$(get_prop mountpoint $pool/$TESTFS)
	fdir=$mntpnt/files
	log_must mkdir $fdir
	sync_pool $pool
	log_must zpool offline $pool $workdir/disk-1
	log_must mkfiles $fdir/f $NFILES
	set -A ext -- $(ls -i $fdir | sort -n | awk '
	    NR == 1 { print } { p = q; q = $0 } END { print p, q }')
	dobj=${ext[0]}
	d=$fdir/${ext[1]}
	sobj=${ext[2]}
	s=$fdir/${ext[3]}
	s2=$fdir/${ext[5]}
	os=$(get_prop objsetid $pool/$TESTFS)
	log_note "$tag: D is object $dobj, S is $sobj" \
	    "in objset $os"
	log_must dd if=$workdir/expected of=$s bs=128k
	sync_pool $pool
	log_must dd if=$s of=$s2 bs=128k
	sync_pool $pool
	saved=$(get_pool_prop dedupsaved $pool)
	(( saved > 0 )) || log_fail "$tag: S2 did not dedup"

	log_must_busy zpool export $pool
	log_must zpool import -d $workdir $pool
	[[ $arm == ahead ]] &&
	    log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zinject -d $workdir/disk-0 -D $DELAY_MS:1 -T read \
	    $pool
	log_must zpool online $pool $workdir/disk-1
	if [[ $arm != ahead ]] && ! freeze_past $pool $os $dobj; then
		log_note "$tag: try $try missed the window," \
		    "no pass started past D in time"
		log_must zinject -c all
		destroy_arm
		return 1
	fi
	sync_pool $pool

	if [[ $arm != nocopy ]]; then
		share $how $s $d
		sync_pool $pool
		(( $(get_pool_prop dedupsaved $pool) > saved )) ||
		    log_fail "$tag: D does not share S's block"
		log_must rm $s $s2
		sync_pool $pool
		sync_pool $pool
		(( $(get_pool_prop dedupsaved $pool) == 0 )) ||
		    log_fail "$tag: S still shares D's block"
		(( $(get_pool_prop dedupused $pool) > 0 )) ||
		    log_fail "$tag: D's block left the DDT"
		(( $(get_pool_prop bcloneused $pool) == 0 )) ||
		    log_fail "$tag: D's block is in the BRT"
	fi

	set -A bm -- $(scan_passes $pool | tail -1)
	txg=${bm[0]:-0}
	log_must zinject -c all
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must zpool wait -t resilver $pool

	kstat dbgmsg | awk -v re="on $pool with class_max = 0;" \
	    'index($0, re) { f = 1 } END { exit !f }' ||
	    log_fail "$tag: the resilver was not incremental"

	# The first pass after the release, and whether a later pass
	# started over.
	set -A bm -- $(scan_passes $pool | awk -v t=$txg '
	    $1 > t && r == "" { r = $0; next }
	    r != "" && $2 == "ddt" { r = r " restarted" }
	    END { print r }')
	log_note "$tag: the resilver resumed at" \
	    "${bm[*]:-nothing}"
	if [[ $arm == ahead ]]; then
		[[ ${bm[1]} == ddt ]] ||
		    log_fail "$tag: the walk began first"
	elif (( ${#bm[@]} != 3 )) || [[ ${bm[1]} != "$os" ]] ||
	    (( bm[2] <= dobj || bm[2] >= sobj )); then
		log_note "$tag: try $try missed the window," \
		    "the walk did not stop between D and S"
		destroy_arm
		return 1
	fi

	log_must zpool detach $pool $workdir/disk-0
	log_must_busy zpool export $pool
	log_must zpool import -d $workdir $pool
	[[ $arm == nocopy ]] && d=$s
	log_must cmp $workdir/expected $d
	destroy_arm
	return 0
}

log_assert "A resilver copies a dedup block written behind it"

workdir=$(mktemp -d $TEST_BASE_DIR/dedup_resilver_behind.XXXXXX) ||
    log_fail "cannot create test directory"
pool=
log_onexit cleanup

log_must save_tunable DBGMSG_ENABLE
log_must save_tunable TXG_TIMEOUT
log_must save_tunable NO_SCRUB_PREFETCH
log_must save_tunable SCAN_LEGACY
log_must save_tunable SCAN_SUSPEND_PROGRESS
log_must set_tunable32 DBGMSG_ENABLE 1
log_must set_tunable32 TXG_TIMEOUT 1
log_must set_tunable32 NO_SCRUB_PREFETCH 1
log_must set_tunable32 SCAN_LEGACY 0
if tunable_exists BCLONE_ENABLED; then
	log_must save_tunable BCLONE_ENABLED
	log_must set_tunable32 BCLONE_ENABLED 1
fi
log_must file_write -o create -f $workdir/expected -b 131072 \
    -c 1 -d 65

set -A runs "fdt dd ahead" "fdt dd nocopy" "fdt dd behind" \
    "legacy dd behind"
if is_freebsd; then
	log_note "copy_file_range can fall back to a copy, skipping clones"
else
	set -A runs -- "${runs[@]}" "fdt clone ahead" \
	    "fdt clone behind"
fi

typeset -i n
for run in "${runs[@]}"; do
	set -- $run
	for (( n = 1; n <= TRIES; n++ )); do
		run_arm $1 $2 $3 $n && break
	done
	(( n <= TRIES )) ||
	    log_fail "$run: did not hit the window in $TRIES tries"
	log_note "$run: needed $n of $TRIES tries"
done

log_pass "A resilver copied a dedup block written behind it"
