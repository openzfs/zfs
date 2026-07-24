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
# Verify that issuing a scrub while a contraction is in progress
# does not cause errors or corruption.
#
# STRATEGY:
# 1. Create anymirror1 pool with 6 disks
# 2. Write 500MiB of data and record checksums
# 3. Start contraction to remove one disk
# 4. Immediately attempt zpool scrub
# 5. Wait for relocation and scrub to complete
# 6. Verify all checksums unchanged
# 7. Verify pool health, no checksum errors
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

log_assert "Scrub during contraction does not cause errors or corruption"

log_note "DEBUG: creating pool with 6 disks"
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}

#
# Write substantial data and record checksums.
#
log_note "DEBUG: writing 500MiB of test data"
typeset -i file_count=10
typeset -i idx=0
set -A cksums

while (( idx < file_count )); do
	log_note "DEBUG: writing file.$idx (50MiB)"
	log_must file_write -o create -b 1048576 -c 50 -d 'R' \
		-f /$TESTPOOL/file.$idx
	cksums[$idx]=$(xxh128digest /$TESTPOOL/file.$idx)
	log_note "DEBUG: file.$idx checksum=${cksums[$idx]}"
	(( idx = idx + 1 ))
done

#
# Start contraction to remove disk 5.
#
log_note "DEBUG: starting contraction to remove vdev_file.5"
log_must zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.5

#
# Immediately attempt a scrub. The contraction may auto-start a scrub
# upon completion, so this may fail with "currently scrubbing" which
# is acceptable.
#
log_note "DEBUG: attempting scrub during contraction"
zpool scrub $TESTPOOL
if [[ $? -ne 0 ]]; then
	log_note "DEBUG: scrub already in progress or contraction completed, continuing"
fi

log_note "DEBUG: waiting for relocation and scrub to complete"
log_must zpool wait -t anyraid_relocate $TESTPOOL
zpool wait -t scrub $TESTPOOL
log_must zpool sync $TESTPOOL

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
log_note "DEBUG: checking pool status"
log_must check_pool_status $TESTPOOL state ONLINE true
cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
[[ "$cksum_count" -eq 0 ]] || log_fail "checksum errors detected"

log_pass "Scrub during contraction does not cause errors or corruption"
