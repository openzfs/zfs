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
# Multiple sequential contractions on anymirror1, removing disks one
# at a time until reaching the minimum width. The final contraction
# attempt (which would go below minimum) must fail.
#
# STRATEGY:
# 1. Create anymirror1 pool with 6 disks
# 2. Write small data and record checksums
# 3. Contract disk 5 (6 -> 5 disks), wait, verify
# 4. Contract disk 4 (5 -> 4 disks), wait, verify
# 5. Contract disk 3 (4 -> 3 disks), wait, verify
# 6. Contract disk 2 (3 -> 2 = logical width), wait, verify
# 7. Attempt to contract disk 1 (2 -> 1, below width), expect failure
# 8. Verify all checksums still intact after 4 successful contractions
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

log_assert "Multiple sequential contractions preserve data until minimum width"

log_note "DEBUG: creating pool with 6 disks"
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}

#
# Write small data and record checksums.
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

#
# Contraction 1: Remove disk 5 (6 -> 5 disks)
#
log_note "DEBUG: contraction 1 - removing vdev_file.5 (6 -> 5 disks)"
log_must zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.5
log_must zpool wait -t anyraid_relocate $TESTPOOL
log_must zpool sync $TESTPOOL

log_note "DEBUG: verifying checksums after contraction 1"
idx=0
while (( idx < file_count )); do
	newcksum=$(xxh128digest /$TESTPOOL/file.$idx)
	[[ "$newcksum" == "${cksums[$idx]}" ]] || \
		log_fail "Contraction 1: checksum mismatch for file.$idx"
	(( idx = idx + 1 ))
done
log_note "DEBUG: contraction 1 checksums verified"

#
# Contraction 2: Remove disk 4 (5 -> 4 disks)
#
log_note "DEBUG: contraction 2 - removing vdev_file.4 (5 -> 4 disks)"
log_must zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.4
log_must zpool wait -t anyraid_relocate $TESTPOOL
log_must zpool sync $TESTPOOL

log_note "DEBUG: verifying checksums after contraction 2"
idx=0
while (( idx < file_count )); do
	newcksum=$(xxh128digest /$TESTPOOL/file.$idx)
	[[ "$newcksum" == "${cksums[$idx]}" ]] || \
		log_fail "Contraction 2: checksum mismatch for file.$idx"
	(( idx = idx + 1 ))
done
log_note "DEBUG: contraction 2 checksums verified"

#
# Contraction 3: Remove disk 3 (4 -> 3 disks)
#
log_note "DEBUG: contraction 3 - removing vdev_file.3 (4 -> 3 disks)"
log_must zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.3
log_must zpool wait -t anyraid_relocate $TESTPOOL
log_must zpool sync $TESTPOOL

log_note "DEBUG: verifying checksums after contraction 3"
idx=0
while (( idx < file_count )); do
	newcksum=$(xxh128digest /$TESTPOOL/file.$idx)
	[[ "$newcksum" == "${cksums[$idx]}" ]] || \
		log_fail "Contraction 3: checksum mismatch for file.$idx"
	(( idx = idx + 1 ))
done
log_note "DEBUG: contraction 3 checksums verified"

#
# Contraction 4: Remove disk 2 (3 -> 2 = logical width).
# This is allowed because children(3) != width(2).
#
log_note "DEBUG: contraction 4 - removing vdev_file.2 (3 -> 2 disks)"
log_must zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.2
log_must zpool wait -t anyraid_relocate $TESTPOOL
log_must zpool sync $TESTPOOL

log_note "DEBUG: verifying checksums after contraction 4"
idx=0
while (( idx < file_count )); do
	newcksum=$(xxh128digest /$TESTPOOL/file.$idx)
	[[ "$newcksum" == "${cksums[$idx]}" ]] || \
		log_fail "Contraction 4: checksum mismatch for file.$idx"
	(( idx = idx + 1 ))
done
log_note "DEBUG: contraction 4 checksums verified"

#
# Contraction 5: Attempt to remove disk 1 (2 -> 1 = below width).
# This must fail because anymirror1 has logical width 2 (parity + 1)
# and children(2) == width(2) triggers ENODEV.
#
log_note "DEBUG: contraction 5 - attempting to remove vdev_file.1 (2 -> 1 = below min)"
log_mustnot zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.1
log_note "DEBUG: contraction 5 correctly rejected"

#
# Final verification: all checksums still intact.
#
log_note "DEBUG: final checksum verification"
idx=0
while (( idx < file_count )); do
	newcksum=$(xxh128digest /$TESTPOOL/file.$idx)
	[[ "$newcksum" == "${cksums[$idx]}" ]] || \
		log_fail "Final: checksum mismatch for file.$idx"
	(( idx = idx + 1 ))
done

log_note "DEBUG: running final scrub"
log_must zpool scrub -w $TESTPOOL

log_note "DEBUG: checking pool status"
log_must check_pool_status $TESTPOOL state ONLINE true
cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
[[ "$cksum_count" -eq 0 ]] || log_fail "checksum errors detected"

log_pass "Multiple sequential contractions preserve data until minimum width"
