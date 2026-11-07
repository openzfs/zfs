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
#	The available space must not drop while a txg holding block clones
#	syncs.  Cloned blocks are charged to their dataset as they are born
#	in syncing context, and the BRT gives the same amount back as saved
#	space.  If the saved space only reaches the pool's free space at the
#	end of the sync, then for the rest of the sync the pool looks short
#	by the whole size of the cloned data, and creating a file fails with
#	ENOSPC (zpool_prefetch_002_pos hit this in CI).
#
# STRATEGY:
#	1. Create a 1 GiB pool and write a 128 MiB file.
#	2. Clone it 16 times (2 GiB logical), so the clones alone are more
#	   than the pool's free space.
#	3. Delay writes with zinject and start a 'zpool sync', so the sync
#	   of the clones stays in progress.
#	4. Once the clones have been charged to the dataset, check that the
#	   available space has not dropped and that a file can be created,
#	   while the sync is still running.
#	5. Clear the delay, let the sync finish, and check the space, the
#	   saved space, the new file and the clones' contents.
#

verify_runnable "global"

claim="Available space doesn't drop while block clones sync."

log_assert $claim

VDEV=$TEST_BASE_DIR/bclone_avail.vdev
FS=$TESTPOOL/fs
MNT=/$FS
NCLONES=16
SRCMB=128
syncpid=""

function cleanup
{
	zinject -c all >/dev/null 2>&1
	[[ -n "$syncpid" ]] && wait $syncpid
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $VDEV
	restore_tunable TXG_TIMEOUT
}

log_onexit cleanup

# Only the 'zpool sync' below may sync the clones.
log_must save_tunable TXG_TIMEOUT
log_must set_tunable64 TXG_TIMEOUT 3600

log_must truncate -s 1G $VDEV
log_must zpool create -o feature@block_cloning=enabled -O recordsize=128k \
    $TESTPOOL $VDEV
log_must zfs create $FS

log_must dd if=/dev/urandom of=$MNT/src bs=1M count=$SRCMB
log_must sync_pool $TESTPOOL

avail0=$(get_prop available $FS)
used0=$(get_prop used $FS)
log_note "before cloning: available=$avail0 used=$used0"

typeset -i i=0
while (( i < NCLONES )); do
	log_must clonefile -f $MNT/src $MNT/clone.$i
	(( i += 1 ))
done
cloned=$(( NCLONES * SRCMB * 1024 * 1024 ))
log_must [ $cloned -gt $avail0 ]

# Keep the sync of the clones going: every write takes a second.
log_must zinject -d $VDEV -D 1000:1 -T write $TESTPOOL
zpool sync $TESTPOOL &
syncpid=$!

# Wait until the clones are born, i.e. charged to the dataset.
want=$(( used0 + cloned / 2 ))
typeset -i n=0
used=$(get_prop used $FS)
while (( used < want )); do
	(( n += 1 ))
	(( n > 600 )) && log_fail "clones not charged after 60s: used=$used"
	sleep 0.1
	used=$(get_prop used $FS)
done
avail=$(get_prop available $FS)
log_note "mid-sync: available=$avail used=$used"

create_err=""
out=$(touch $MNT/newfile 2>&1) || create_err="${out:-touch failed}"

# The checks above only count if the sync was still in progress.
kill -0 $syncpid 2>/dev/null || \
    log_fail "the sync finished before the checks, the test proves nothing"

[[ -z "$create_err" ]] || \
    log_fail "creating a file failed while the clones synced: $create_err"
# Allow for metadata written by the sync, nothing like the cloned size.
log_must [ $avail -ge $(( avail0 - 64 * 1024 * 1024 )) ]

log_must zinject -c all
log_must wait $syncpid
syncpid=""
log_must sync_pool $TESTPOOL

avail1=$(get_prop available $FS)
used1=$(get_prop used $FS)
saved=$(zpool get -Hp -o value bclone_saved $TESTPOOL)
log_note "after sync: available=$avail1 used=$used1 saved=$saved"
log_must [ $saved -eq $cloned ]
log_must [ $used1 -ge $(( used0 + cloned )) ]
log_must [ $avail1 -ge $(( avail0 - 64 * 1024 * 1024 )) ]
log_must [ -f $MNT/newfile ]
log_must touch $MNT/newfile2
log_must have_same_content $MNT/src $MNT/clone.0
log_must have_same_content $MNT/src $MNT/clone.$(( NCLONES - 1 ))

log_pass $claim
