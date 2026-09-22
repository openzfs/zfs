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
#	Freeing one reference to a cloned or deduplicated block while a
#	resilver has queued the block must not drop the queued read. The block
#	still has another reference, and the resilver must copy it.
#
# STRATEGY:
#	1. Write a large file, then a one-block file and a clone or a
#	   deduplicated copy of it.
#	2. Delay reads of the source, and attach a new device. The sorted
#	   resilver issues the largest extents first, the large file's, so the
#	   shared block stays queued after traversal has finished.
#	3. Remove the clone or copy while its block is queued.
#	4. Detach the source, and verify the remaining file cold.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	[[ -n $pool ]] && destroy_pool $pool
	[[ -n $orig_dbgmsg ]] &&
	    set_tunable32 DBGMSG_ENABLE $orig_dbgmsg >/dev/null 2>&1
	[[ -n $orig_legacy ]] &&
	    set_tunable32 SCAN_LEGACY $orig_legacy >/dev/null 2>&1
	[[ -n $orig_strategy ]] &&
	    set_tunable32 SCAN_ISSUE_STRATEGY $orig_strategy >/dev/null 2>&1
	[[ -n $orig_vdev_limit ]] &&
	    set_tunable64 SCAN_VDEV_LIMIT $orig_vdev_limit >/dev/null 2>&1
	rm -rf $workdir
}

function traversal_finished
{
	kstat dbgmsg | grep -q "scan complete for $pool txg "
}

log_assert "A queued resilver read survives a freed reference to its block"
workdir=$(mktemp -d $TEST_BASE_DIR/block_cloning_resilver_free.XXXXXX) ||
    log_fail "cannot create test directory"
log_onexit cleanup

pool=
orig_dbgmsg=$(get_tunable DBGMSG_ENABLE) ||
    log_fail "cannot read DBGMSG_ENABLE"
orig_legacy=$(get_tunable SCAN_LEGACY) ||
    log_fail "cannot read SCAN_LEGACY"
orig_strategy=$(get_tunable SCAN_ISSUE_STRATEGY) ||
    log_fail "cannot read SCAN_ISSUE_STRATEGY"
orig_vdev_limit=$(get_tunable SCAN_VDEV_LIMIT) ||
    log_fail "cannot read SCAN_VDEV_LIMIT"
# Issuing in LBA order could read the shared block before the large file.
log_must set_tunable32 DBGMSG_ENABLE 1
log_must set_tunable32 SCAN_LEGACY 0
log_must set_tunable32 SCAN_ISSUE_STRATEGY 2
# Keep the 128K shared block queued behind the 64M filler after traversal.
log_must set_tunable64 SCAN_VDEV_LIMIT 1048576
log_must file_write -o create -f $workdir/expected -b 131072 -c 1 -d 65

for share in clone dedup; do
	pool="$TESTPOOL1-${workdir##*.}-$share"
	log_must truncate -s 512M $workdir/disk-0 $workdir/disk-1
	log_must zpool create -o feature@block_cloning=enabled $pool \
	    $workdir/disk-0
	dedup=off
	[[ $share == dedup ]] && dedup=on
	log_must zfs create -o compression=off -o recordsize=128k \
	    -o dedup=off $pool/$TESTFS
	mntpnt=$(get_prop mountpoint $pool/$TESTFS)
	# Keep the filler allocated independently of the shared-block case.
	log_must file_write -o create -f $mntpnt/filler \
	    -b 1048576 -c 64 -d 90
	sync_pool $pool
	log_must zfs set dedup=$dedup $pool/$TESTFS
	log_must cp $workdir/expected $mntpnt/file
	sync_pool $pool
	if [[ $share == clone ]]; then
		log_must clonefile -f $mntpnt/file $mntpnt/copy
	else
		# Rewrite the data rather than letting cp clone it.
		log_must dd if=$mntpnt/file of=$mntpnt/copy bs=128k
	fi
	sync_pool $pool
	blocks=$(get_same_blocks $pool/$TESTFS file $pool/$TESTFS copy) ||
	    log_fail "cannot inspect $share block sharing"
	[[ $blocks == 0 ]] ||
	    log_fail "$share files do not share block zero: $blocks"

	log_mustnot traversal_finished
	log_must zinject -d $workdir/disk-0 -D 200:1 -T read $pool
	log_must zpool attach $pool $workdir/disk-0 $workdir/disk-1
	typeset -i i
	for (( i = 0; i < 300; i++ )); do
		traversal_finished && break
		sleep 0.1
	done
	if (( i == 300 )); then
		kstat dbgmsg >$workdir/dbgmsg-$share
		cat $workdir/dbgmsg-$share
		log_fail "$share resilver traversal did not finish"
	fi
	log_must is_pool_resilvering $pool
	log_must rm $mntpnt/copy
	sync_pool $pool
	log_must zinject -c all
	log_must zpool wait -t resilver $pool

	log_must zpool detach $pool $workdir/disk-0
	log_must zpool export $pool
	log_must zpool import -d $workdir $pool
	log_must cmp $workdir/expected $mntpnt/file
	log_must zpool destroy $pool
	pool=
	log_must rm $workdir/disk-0 $workdir/disk-1
done

log_pass "A queued resilver read survived a freed reference to its block"
