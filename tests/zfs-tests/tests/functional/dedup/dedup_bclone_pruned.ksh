#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2026, TrueNAS.
#

#
# DESCRIPTION:
#	Verify that block cloning works correctly when the DDT entry for a
#	dedup block has been pruned.  When a block has the DEDUP bit set but
#	no DDT entry (because it was pruned), cloning it must create a BRT
#	entry to track the extra reference.  Freeing the original must
#	consult the BRT rather than proceeding directly to a DVA free,
#	otherwise the block is freed while the clone still references it.
#
# STRATEGY:
#	1. Create a pool with both dedup and block_cloning enabled
#	2. Write a file with dedup=on so blocks get DEDUP bit set in their BPs
#	3. Prune the DDT to remove those entries (blocks remain, DEDUP bit
#	   stays set in block pointers)
#	4. Clone the file - brt_pending_apply_vdev() must fall back to BRT
#	   since ddt_addref() returns B_FALSE for pruned entries
#	5. Write a second copy via dd - same hash, new physical blocks, new
#	   DDT entries at different DVAs from the BRT-tracked blocks
#	6. Delete the clone first - must go through BRT, not DDT, even though
#	   a matching DDT entry now exists for the same hash
#	7. Delete the dd copy - DDT entries freed normally
#	8. Delete the original - no DDT entry, no BRT entry, DVA freed
#	9. Verify reference counts with zdb -b at each step
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

log_assert "Block cloning of dedup blocks with pruned DDT entries uses BRT"

# Flush DDT log every TXG so entries appear in the ZAP immediately,
# making ddtprune effective and test behavior predictable.
log_must save_tunable DEDUP_LOG_TXG_MAX
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1
log_must save_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
log_must set_tunable32 DEDUP_LOG_FLUSH_ENTRIES_MIN 100000

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	log_must restore_tunable DEDUP_LOG_TXG_MAX
	log_must restore_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
}

log_onexit cleanup

log_must zpool create -f -o feature@block_cloning=enabled $TESTPOOL $DISKS

log_must zfs create -o dedup=sha256 -o recordsize=128k $TESTPOOL/$TESTFS
typeset mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)

# Write unique data: each block gets a DDT entry with refcnt=1.
log_must dd if=/dev/urandom of=$mountpoint/file1 bs=128k count=8

sync_pool $TESTPOOL

# Verify DDT has entries before pruning.
typeset entries=$(zpool status -D $TESTPOOL | \
    grep "dedup: DDT entries" | awk '{print $4}')
log_must test "$entries" -eq 8

# Sleep 1s so the DDT entries are at least 1 second old.  ddtprune uses
# an age-based cutoff and will silently skip entries that are too fresh.
sleep 1

# Prune all unique (refcnt=1) entries.  The blocks remain on disk and the
# block pointers in file1 still have the DEDUP bit set, but there is no
# longer a DDT entry for them.
log_must zpool ddtprune -p 100 $TESTPOOL
sync_pool $TESTPOOL

# Confirm the prune actually removed all entries.
entries=$(zpool status -D $TESTPOOL | \
    grep "dedup: DDT entries" | awk '{print $4}')
[[ -z "$entries" || "$entries" -eq 0 ]] || \
    log_fail "DDT entries not pruned: $entries remain"

# Clone file1.  brt_pending_apply_vdev() will see the DEDUP bit, call
# ddt_addref(), receive B_FALSE (no DDT entry), and fall through to
# create BRT entries instead.
log_must clonefile -f $mountpoint/file1 $mountpoint/clone1
sync_pool $TESTPOOL

# BRT entries exist; reference counts must be consistent.
log_must zdb -b $TESTPOOL

# Write a second copy via dd.  Since the DDT was pruned, dedup can't find
# an existing entry and writes new physical blocks at new DVAs, creating
# fresh DDT entries with refcnt=1.  The BRT-tracked blocks (file1/clone1)
# are at the old DVAs and are unaffected.
log_must dd if=$mountpoint/file1 of=$mountpoint/file2 bs=128k
sync_pool $TESTPOOL

# Eight new unique DDT entries (file2's blocks); BRT still holds refs for
# file1/clone1's old blocks.
typeset entries=$(zpool status -D $TESTPOOL | \
    grep "dedup: DDT entries" | awk '{print $4}')
log_must test "$entries" -eq 8
log_must zdb -b $TESTPOOL

# Delete the clone first.  Its blocks carry the DEDUP bit and the same
# hash as file2's DDT entries, but the DVAs differ — the free must go
# through BRT, not DDT, leaving file2's DDT entries intact.
log_must rm $mountpoint/clone1
sync_pool $TESTPOOL

entries=$(zpool status -D $TESTPOOL | \
    grep "dedup: DDT entries" | awk '{print $4}')
log_must test "$entries" -eq 8
log_must zdb -b $TESTPOOL

# Delete file2.  DDT entries freed; file1's BRT-tracked blocks unaffected.
log_must rm $mountpoint/file2
sync_pool $TESTPOOL
log_must eval "zdb -D $TESTPOOL | grep -q 'All DDTs are empty'"
log_must zdb -b $TESTPOOL

# Delete the original.  No DDT entry, no BRT entry; DVA freed directly.
log_must rm $mountpoint/file1
sync_pool $TESTPOOL
log_must zdb -b $TESTPOOL

log_pass "Block cloning of dedup blocks with pruned DDT entries uses BRT"
