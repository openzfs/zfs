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

# DESCRIPTION:
#	With fast dedup, the last decref of a block lands in the DDT log
#	and the physical free happens only when the log flushes. Blocks
#	inside that window are referenced by no block pointer but still
#	allocated, and zdb used to misreport them: as "leaked space" in
#	the generic accounting, and as a fatal "obsolete indirect mapping
#	count mismatch" when such a block sits behind a removed vdev's
#	mapping. Verify zdb accounts pending DDT-log frees instead — and
#	only those: live entries and pruned-entry tombstones in the log
#	must not be claimed.
#
# STRATEGY:
#	1. Write dedup'd blocks (one shared twice, one unique) with the
#	   DDT log flushing every txg, so the entries land in the ZAP.
#	2. Pin the log, ddtprune the unique entries (leaving zero-phys
#	   tombstones), and drop one reference to the shared blocks
#	   (leaving a positive-refcount log entry). Neither describes a
#	   pending free.
#	3. Pre-delete zdb: the "DDT pending free" bucket must be empty and
#	   nothing may be double-claimed — this exercises both the birth
#	   and refcount exclusions.
#	4. Drop the last reference to the shared blocks; their
#	   decref-to-zero entries stay pending in the pinned log.
#	5. Post-delete zdb: exactly those blocks must be accounted in the
#	   "DDT pending free" bucket with no leaked space.
#	6. Write the same content again before the log flushes: the pending
#	   entries are resurrected, so nothing may remain pending.
#	7. Repeat the window on a pool whose data was migrated by device
#	   removal, so the pending frees point at an indirect vdev: zdb
#	   must exit 0 with no obsolete mapping count mismatch.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

verify_runnable "global"
verify_disk_count "$DISKS" 2

log_assert "zdb accounts pending DDT-log frees instead of reporting leaks"

log_must save_tunable DEDUP_LOG_TXG_MAX
log_must save_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN

# Content is written from a seed outside the pool so it can be written
# again later, to resurrect an entry that is pending free.
typeset seed=$TEST_BASE_DIR/dedup_log_zdb_leak.seed

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	rm -f $seed
	log_must restore_tunable DEDUP_LOG_TXG_MAX
	log_must restore_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
}

log_onexit cleanup

set -A disks $DISKS

# Extract the block count of the "DDT pending free" bucket; the table
# prints "-" when the bucket is empty.
function pending_count
{
	echo "$1" | awk '/DDT pending free/ {print $1; exit}'
}

# True when the DDT log holds at least one entry. zdb's status is checked
# separately, so a zdb failure cannot be hidden by grep finding nothing.
function ddt_log_has_entries
{
	typeset out
	out=$(zdb -D $TESTPOOL 2>&1) || log_fail "zdb -D failed: $out"
	echo "$out" | grep -q 'DDT-log-.*entries=[1-9]'
}

# Phase 1: pending frees on a concrete vdev must show up as "DDT pending
# free" — and nothing else from the log (live entries, prune tombstones)
# may be claimed.
log_must zpool create -f -o feature@fast_dedup=enabled \
    -O dedup=on -O compression=off -O xattr=sa $TESTPOOL ${disks[0]}
log_must zfs create -o recordsize=128k $TESTPOOL/$TESTFS
typeset mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)

# Flush the log every txg while writing, so the entries land in the
# DDT ZAP where ddtprune can see them.
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1
log_must set_tunable32 DEDUP_LOG_FLUSH_ENTRIES_MIN 100000
log_must dd if=/dev/urandom of=$seed bs=128k count=4
log_must cp $seed $mountpoint/file1
log_must cp $seed $mountpoint/file2
log_must dd if=/dev/urandom of=$mountpoint/file3 bs=128k count=4
sync_pool $TESTPOOL
sync_pool $TESTPOOL
# The log must be empty here (everything flushed to the ZAP); capture
# zdb's output and status separately so a zdb failure can't be masked by
# the grep finding nothing.
ddt_out=$(zdb -D $TESTPOOL 2>&1)
log_must test $? -eq 0
log_mustnot eval "echo \"\$ddt_out\" | grep -q 'DDT-log-.*entries=[1-9]'"

# Age the entries past ddtprune's cutoff, then pin the log so that
# everything logged from here on stays pending.
sleep 1
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1000000

# Prune file3's unique entries: the pinned log now holds zero-phys
# tombstones for blocks that file3 still references. Then drop one of
# the two references to the shared blocks, so the log also holds a
# positive-refcount (2->1) entry for blocks that file2 still references.
log_must zpool ddtprune -p 100 $TESTPOOL
sync_pool $TESTPOOL
# Assert the tombstones separately, before anything is dereferenced: at
# this point a log entry can only have come from the prune. Checking only
# after the rm below would be satisfied by the 2->1 decref alone.
log_must ddt_log_has_entries

log_must rm $mountpoint/file1
sync_pool $TESTPOOL
log_must ddt_log_has_entries

# Pre-delete: no block is pending free yet. The log holds only
# tombstones (birth 0) and a positive-refcount entry — neither may be
# claimed. A bogus claim would collide with the traversal claiming the
# still-live file2/file3 blocks and fail zdb outright, and would show a
# nonzero "DDT pending free" bucket.
zdb_out=$(zdb -bbcc $TESTPOOL 2>&1)
rc=$?
echo "$zdb_out"
if (( rc != 0 )) || echo "$zdb_out" | grep -q "leaked space"; then
	log_fail "zdb claimed a tombstone or a live log entry as pending"
fi
typeset pending=$(pending_count "$zdb_out")
[[ "$pending" == "-" ]] || \
    log_fail "blocks claimed as pending while none are: '$pending'"

# Drop the last reference; the shared blocks' decref-to-zero entries
# now stay pending in the pinned log. There are four such blocks.
log_must rm $mountpoint/file2
sync_pool $TESTPOOL
log_must ddt_log_has_entries

zdb_out=$(zdb -bbcc $TESTPOOL 2>&1)
rc=$?
echo "$zdb_out"
if (( rc != 0 )) || echo "$zdb_out" | grep -q "leaked space"; then
	log_fail "zdb misreported pending DDT-log frees as leaks"
fi
log_must eval "echo \"\$zdb_out\" | grep -q 'No leaks'"
pending=$(pending_count "$zdb_out")
[[ "$pending" == "4" ]] || \
    log_fail "expected 4 blocks in the DDT pending free bucket, got '$pending'"

# Writing the same content again while those entries are still pending
# must resurrect them: the log-aware lookup finds the zero-refcount entry
# and bumps it, so nothing is pending and nothing has been freed.
log_must cp $seed $mountpoint/file4
sync_pool $TESTPOOL

zdb_out=$(zdb -bbcc $TESTPOOL 2>&1)
rc=$?
echo "$zdb_out"
if (( rc != 0 )) || echo "$zdb_out" | grep -q "leaked space"; then
	log_fail "zdb misreported a resurrected DDT-log entry"
fi
pending=$(pending_count "$zdb_out")
[[ "$pending" == "-" ]] || \
    log_fail "blocks still pending after resurrection: '$pending'"
log_must cmp_xxh128 $mountpoint/file4 $seed

# Phase 2: the same window behind a removed vdev's indirect mapping used
# to fail zdb outright with an obsolete mapping count mismatch.
destroy_pool $TESTPOOL
log_must zpool create -f -o feature@fast_dedup=enabled \
    -O dedup=on -O compression=off -O xattr=sa $TESTPOOL ${disks[0]}
log_must zfs create -o recordsize=128k $TESTPOOL/$TESTFS
mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must dd if=/dev/urandom of=$mountpoint/file1 bs=128k count=4
log_must dd if=$mountpoint/file1 of=$mountpoint/file2 bs=128k
sync_pool $TESTPOOL

# Migrate everything to a second disk; the dedup'd blocks now live
# behind the removed vdev's indirect mapping.
log_must zpool add $TESTPOOL ${disks[1]}
log_must zpool remove $TESTPOOL ${disks[0]}
log_must wait_for_removal $TESTPOOL

log_must rm $mountpoint/file1 $mountpoint/file2
sync_pool $TESTPOOL
log_must ddt_log_has_entries

zdb_out=$(zdb -bcc $TESTPOOL 2>&1)
rc=$?
echo "$zdb_out"
if (( rc != 0 )) || \
    echo "$zdb_out" | grep -q "obsolete indirect mapping count mismatch"; then
	log_fail "pending DDT-log frees tripped the obsolete mapping check"
fi

log_pass "zdb accounts pending DDT-log frees instead of reporting leaks"
