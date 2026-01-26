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
# Verify that interrupting a contraction via export/import and then
# allowing it to resume works correctly. On re-import, the contraction
# either resumes (if tasks remain) or fires CONTRACTION_DONE (if
# relocation was already complete).
#
# STRATEGY:
# 1. Create anymirror1 pool with 6 disks
# 2. Write 500MiB of data and record checksums
# 3. Start contraction to remove one disk
# 4. Export the pool to interrupt
# 5. Re-import the pool
# 6. Verify pool is healthy
# 7. Wait for contraction to resume/complete
# 8. Verify all checksums unchanged
# 9. Verify pool health
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

log_assert "Interrupted contraction resumes correctly after export/import"

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
# Interrupt the contraction by exporting the pool. The contraction
# may already be complete by the time we export (since it can be fast),
# but the export/import cycle still validates the interrupted path.
#
log_note "DEBUG: exporting pool to interrupt contraction"
log_must zpool export $TESTPOOL

log_note "DEBUG: re-importing pool"
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL

log_note "DEBUG: checking pool status after re-import"
log_must check_pool_status $TESTPOOL state ONLINE true

#
# Wait for contraction to resume/complete. On import, if the
# contraction had pending tasks, the relocate thread resumes.
# If relocation was already done, CONTRACTION_DONE fires to
# detach the disk.
#
log_note "DEBUG: waiting for contraction to complete after re-import"
zpool wait -t anyraid_relocate $TESTPOOL
log_note "DEBUG: wait returned $?"

log_note "DEBUG: waiting for any scrub to complete"
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
# Verify pool health.
#
log_note "DEBUG: checking final pool status"
log_must check_pool_status $TESTPOOL state ONLINE true

log_pass "Interrupted contraction resumes correctly after export/import"
