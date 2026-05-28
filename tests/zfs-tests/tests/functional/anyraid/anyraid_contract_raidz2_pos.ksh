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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify contraction works on anyraidz2:2 (double parity).
# Minimum width = 4 (2 parity + 2 data), so 6 disks has 2 surplus.
#
# STRATEGY:
# 1. Create anyraidz2:2 pool with 6 disks
# 2. Write data and record checksums
# 3. Contract one disk (6 -> 5), wait, verify
# 4. Scrub, verify no errors
#

verify_runnable "global"

cleanup() {
	log_note "DEBUG: cleanup started"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}
}

log_onexit cleanup

log_note "DEBUG: creating sparse files"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}

log_note "DEBUG: setting tile size to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Contraction on anyraidz2:2 preserves data integrity"

log_note "DEBUG: creating anyraidz2:2 pool with 6 disks"
log_must create_pool $TESTPOOL anyraidz2:2 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}

#
# Write data and record checksums.
#
log_note "DEBUG: writing test files and recording checksums"
typeset -i file_count=10
typeset -i idx=0
set -A cksums

while (( idx < file_count )); do
	log_must file_write -o create -b 1048576 -c 1 -d 'R' \
		-f /$TESTPOOL/file.$idx
	cksums[$idx]=$(xxh128digest /$TESTPOOL/file.$idx)
	log_note "DEBUG: file.$idx checksum=${cksums[$idx]}"
	(( idx = idx + 1 ))
done

log_note "DEBUG: recording capacity before contraction"
cap_before=$(zpool get -Hp -o value size $TESTPOOL)
log_note "DEBUG: capacity before=$cap_before"

#
# Get the actual vdev name from zpool status for anyraidz.
#
log_note "DEBUG: looking up anyraidz vdev name"
vdev_name=$(zpool status $TESTPOOL | awk '/anyraidz/ && /raidz/ {print $1; exit}')
log_note "DEBUG: vdev name=$vdev_name"
[[ -n "$vdev_name" ]] || log_fail "Could not find anyraidz vdev name"

#
# Contract the pool by removing disk 5 (6 -> 5 disks).
#
log_note "DEBUG: starting contraction to remove vdev_file.5"
log_must zpool contract $TESTPOOL $vdev_name \
	$TEST_BASE_DIR/vdev_file.5

log_note "DEBUG: waiting for relocation to complete"
log_must zpool wait -t anyraid_relocate $TESTPOOL
log_must zpool sync $TESTPOOL

#
# Verify capacity decreased.
#
log_note "DEBUG: checking capacity after contraction"
cap_after=$(zpool get -Hp -o value size $TESTPOOL)
log_note "DEBUG: capacity after=$cap_after"
[[ "$cap_after" -lt "$cap_before" ]] || \
	log_fail "Capacity did not decrease: before=$cap_before after=$cap_after"

#
# Verify all file checksums are unchanged.
#
log_note "DEBUG: verifying file checksums"
idx=0
while (( idx < file_count )); do
	newcksum=$(xxh128digest /$TESTPOOL/file.$idx)
	[[ "$newcksum" == "${cksums[$idx]}" ]] || \
		log_fail "Checksum mismatch for file.$idx: expected=${cksums[$idx]} got=$newcksum"
	(( idx = idx + 1 ))
done

#
# Verify pool health and no checksum errors.
#
log_note "DEBUG: running scrub"
log_must zpool scrub -w $TESTPOOL

log_note "DEBUG: checking pool status"
log_must check_pool_status $TESTPOOL state ONLINE true
cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
[[ "$cksum_count" -eq 0 ]] || log_fail "checksum errors detected"

log_pass "Contraction on anyraidz2:2 preserves data integrity"
