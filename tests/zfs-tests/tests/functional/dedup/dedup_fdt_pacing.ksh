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
# Copyright (c) 2025 Klara, Inc.
#

# Ensure dedup log flushes are appropriately paced

. $STF_SUITE/include/libtest.shlib

log_assert "dedup (FDT) paces out log entries appropriately"

function get_ddt_log_entries
{
	zdb -D $TESTPOOL | grep -- "-log-sha256-" | sed 's/.*entries=//' | \
	    awk '{sum += $1} END {print sum}'
}

function cleanup
{
	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi
	log_must restore_tunable DEDUP_LOG_FLUSH_ENTRIES_MAX
}

log_onexit cleanup

# Create a pool with fast dedup enabled. We disable block cloning to ensure
# it doesn't get in the way of dedup.
log_must zpool create -f \
    -o feature@fast_dedup=enabled \
    -o feature@block_cloning=disabled \
    $TESTPOOL $DISKS

# Create a filesystem with a small recordsize so that we get more DDT entries,
# disable compression so our writes create predictable results on disk, and
# use 'xattr=sa' to prevent selinux xattrs influencing our accounting
log_must zfs create \
    -o dedup=on \
    -o compression=off \
    -o xattr=sa \
    -o checksum=sha256 \
    -o recordsize=4k $TESTPOOL/fs

# Set the dedup log to only flush a single entry per txg.
# It's hard to guarantee that exactly one flush will happen per txg, or that
# we don't miss a txg due to weird latency or anything, so we build some
# wiggle room into subsequent checks.

log_must save_tunable DEDUP_LOG_FLUSH_ENTRIES_MAX
log_must set_tunable32 DEDUP_LOG_FLUSH_ENTRIES_MAX 1

# Create a file. This is 256 full blocks, so will produce 256 entries in the
# dedup log.
log_must dd if=/dev/urandom of=/$TESTPOOL/fs/file1 bs=128k count=8
sync_pool

# Verify there are at least 240 entries in the dedup log.
log_entries=$(get_ddt_log_entries)
[[ "$log_entries" -gt 240 ]] || \
    log_fail "Fewer than 240 entries in dedup log: $log_entries"

# Wait for 5 TXGs to sync.
for i in `seq 1 5`; do
	sync_pool
done

# Verify there are at least 220 entries in the dedup log.
log_entries2=$(get_ddt_log_entries)
[[ $((log_entries - log_entries2)) -lt 20 ]] || \
    log_fail "Too many entries pruned from dedup log: " \
    "from $log_entries to $log_entries2"
[[ $((log_entries - log_entries2)) -gt 5 ]] || \
    log_fail "Too few entries pruned from dedup log: " \
    "from $log_entries to $log_entries2"

# Set the log flush rate high enough to clear the whole list.
log_must set_tunable32 DEDUP_LOG_FLUSH_ENTRIES_MAX 1024
sync_pool

# Verify there are 0 entries in the dedup log.
log_entries3=$(get_ddt_log_entries)
[[ "$log_entries3" -eq 0 ]] || \
    log_fail "Entries still present in dedup log: $log_entries3"

# Verify there are 256 entries in the unique table.
log_must eval "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-unique:.*entries=256'"

log_pass "dedup (FDT) paces out log entries appropriately"
