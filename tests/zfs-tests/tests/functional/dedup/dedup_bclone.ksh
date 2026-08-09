#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2026, TrueNAS.
#

#
# DESCRIPTION:
#	Verify that block cloning interacts correctly with dedup when the DDT
#	entry for the block is still present.  In this case brt_pending_apply_vdev()
#	calls ddt_addref() which succeeds, so the extra reference is tracked in
#	the DDT rather than in the BRT.
#
# STRATEGY:
#	1. Create a pool with block_cloning enabled and dedup=on
#	2. Write a file (4 blocks, unique DDT entries, refcnt=1)
#	3. Clone the file - ddt_addref() bumps DDT refcnt to 2, entries move
#	   from unique to duplicate table; no BRT entries are created
#	4. Write a third copy via dd - DDT refcnt becomes 3
#	5. Delete files in sequence, verifying DDT counts and zdb -b at each step
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

log_assert "Block cloning with live DDT entries uses ddt_addref, not BRT"

# Flush DDT log every TXG so entries appear in the ZAP immediately.
log_must save_tunable DEDUP_LOG_TXG_MAX
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	log_must restore_tunable DEDUP_LOG_TXG_MAX
}

log_onexit cleanup

# we disable compression so our writes create predictable results on disk
# Use 'xattr=sa' to prevent selinux xattrs influencing our accounting
log_must zpool create -f \
    -o feature@block_cloning=enabled \
    -O dedup=on \
    -O compression=off \
    -O xattr=sa \
    $TESTPOOL $DISKS

log_must zfs create -o recordsize=128k $TESTPOOL/$TESTFS
typeset mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)

# Write unique data: 4 blocks, each gets a DDT entry with refcnt=1.
log_must dd if=/dev/urandom of=$mountpoint/file1 bs=128k count=4
sync_pool $TESTPOOL

log_must eval "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-unique:.*entries=4'"

# Clone file1.  The extra reference goes into the DDT rather than the BRT.
# The entries move from unique (refcnt=1) to duplicate (refcnt=2).
log_must clonefile -f $mountpoint/file1 $mountpoint/clone1
sync_pool $TESTPOOL

log_must eval \
    "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-duplicate:.*entries=4'"
log_must zdb -b $TESTPOOL

# Write a third copy via dd — DDT refcnt becomes 3.
log_must dd if=$mountpoint/file1 of=$mountpoint/file2 bs=128k
sync_pool $TESTPOOL

log_must eval \
    "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-duplicate:.*entries=4'"
log_must zdb -b $TESTPOOL

# Delete the clone — DDT refcnt drops to 2, still duplicate.
log_must rm $mountpoint/clone1
sync_pool $TESTPOOL

log_must eval \
    "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-duplicate:.*entries=4'"
log_must zdb -b $TESTPOOL

# Delete file2 — DDT refcnt drops to 1, entries move back to unique.
log_must rm $mountpoint/file2
sync_pool $TESTPOOL

log_must eval "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-unique:.*entries=4'"
log_must zdb -b $TESTPOOL

# Delete the original — DDT empty, blocks freed.
log_must rm $mountpoint/file1
sync_pool $TESTPOOL

log_must eval "zdb -D $TESTPOOL | grep -q 'All DDTs are empty'"
log_must zdb -b $TESTPOOL

log_pass "Block cloning with live DDT entries uses ddt_addref, not BRT"
