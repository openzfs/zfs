#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
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
# Copyright (c) 2026 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Anyraid no-op rebalance should return an error when there is nothing
# to rebalance (no new disk attached).
#
# KNOWN ISSUE:
# As of 2026-02-18, calling "zpool rebalance" on a pool with no prior
# "zpool attach" causes the command to hang indefinitely at the kernel
# level, making ZFS unresponsive and requiring a system reboot. This
# test is NOT registered in common.run to prevent hanging the test
# suite. It exists to document the bug and should be run manually by
# developers investigating this issue.
#
# Expected behavior once fixed: zpool rebalance should either return a
# non-zero exit code with a meaningful error message, or complete
# instantly as a no-op.
#
# STRATEGY:
# 1. Create an anymirror1 vdev with equal-sized disks
# 2. Write data and record checksums
# 3. Run rebalance without attaching a new disk
# 4. Verify the command either returns an error or completes cleanly
# 5. Verify all data checksums are unchanged
# 6. Verify that scrub found no errors
#

verify_runnable "global"

cleanup() {
	zpool destroy $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}
}

log_onexit cleanup

log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Anyraid no-op rebalance should return an error or complete cleanly"

log_must create_pool $TESTPOOL anymirror1 $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}

#
# Write several files and record their checksums.
#
typeset -i file_count=10
typeset -i idx=0
set -A cksums

while (( idx < file_count )); do
	log_must file_write -o create -b 1048576 -c 1 -d 'R' \
		-f /$TESTPOOL/file.$idx
	cksums[$idx]=$(xxh128digest /$TESTPOOL/file.$idx)
	(( idx = idx + 1 ))
done

cap_before=$(zpool get -Hp -o value size $TESTPOOL)

#
# Run rebalance on an already-balanced pool (no new disk attached).
#
# WARNING: As of 2026-02-18 this command HANGS at the kernel level.
# If this test completes, the bug has been fixed.
#
zpool rebalance $TESTPOOL anymirror1-0
rebalance_rc=$?

if [[ $rebalance_rc -ne 0 ]]; then
	log_note "zpool rebalance returned error $rebalance_rc (expected for no-op)"
fi

log_must zpool sync $TESTPOOL

#
# Verify capacity is unchanged after a no-op rebalance.
#
cap_after=$(zpool get -Hp -o value size $TESTPOOL)
[[ "$cap_after" -eq "$cap_before" ]] || \
	log_fail "Capacity changed after no-op rebalance: before=$cap_before after=$cap_after"

#
# Verify all file checksums are unchanged.
#
idx=0
while (( idx < file_count )); do
	newcksum=$(xxh128digest /$TESTPOOL/file.$idx)
	[[ "$newcksum" == "${cksums[$idx]}" ]] || \
		log_fail "Checksum mismatch for file.$idx: expected=${cksums[$idx]} got=$newcksum"
	(( idx = idx + 1 ))
done

#
# Run an explicit scrub and verify pool health.
#
log_must zpool scrub $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL

log_must check_pool_status $TESTPOOL state ONLINE true
log_must is_pool_scrubbed $TESTPOOL true
cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
[[ "$cksum_count" -eq 0 ]] || log_fail "checksum errors detected"

log_pass "Anyraid no-op rebalance should return an error or complete cleanly"
