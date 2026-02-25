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
# Copyright (c) 2026, Klara, Inc.
#

. $STF_SUITE/tests/functional/anyraid/anyraid_common.kshlib

#
# DESCRIPTION:
# Offlining a disk from an anymirror1 pool with surplus disks
# preserves data integrity. The pool becomes DEGRADED but all data
# remains readable. After onlining the disk back, the pool returns
# to ONLINE. This test is self-contained.
#
# STRATEGY:
# 1. Create an anymirror1 pool with 3 disks (surplus for parity=1).
# 2. Write data and record xxh128 checksums.
# 3. Offline one disk.
# 4. Verify pool is DEGRADED and all checksums match.
# 5. Online the disk back.
# 6. Wait for resilver, verify pool returns to ONLINE.
# 7. Scrub and verify no errors.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must delete_sparse_files
}

log_onexit cleanup

log_assert "Offlining a disk from an anymirror1 pool with surplus disks preserves data"

#
# Create sparse disk files and pool.
#
log_must create_sparse_files "disk" 3 $DEVSIZE
log_must zpool create -f $TESTPOOL anymirror1 $disk0 $disk1 $disk2

#
# Write files and record checksums.
#
typeset -i atfile=0
set -A files
set -A cksums
typeset newcksum

while (( atfile < FILE_COUNT )); do
	files[$atfile]=/$TESTPOOL/file.$atfile
	log_must file_write -o create -f ${files[$atfile]} \
		-b $FILE_SIZE -c 1
	cksums[$atfile]=$(xxh128digest ${files[$atfile]})
	(( atfile = atfile + 1 ))
done

log_must zpool sync $TESTPOOL

#
# Offline the third disk.
#
log_must zpool offline $TESTPOOL $disk2

#
# Verify the pool is DEGRADED.
#
log_must check_state $TESTPOOL "" "degraded"
log_must check_state $TESTPOOL $disk2 "offline"

#
# Verify all file checksums still match while degraded.
#
atfile=0
typeset -i failedcount=0
while (( atfile < FILE_COUNT )); do
	newcksum=$(xxh128digest ${files[$atfile]})
	if [[ $newcksum != ${cksums[$atfile]} ]]; then
		(( failedcount = failedcount + 1 ))
	fi
	(( atfile = atfile + 1 ))
done

if (( failedcount > 0 )); then
	log_fail "$failedcount of $FILE_COUNT files had wrong checksums" \
		"while pool was degraded"
fi

#
# Online the disk back.
#
log_must zpool online $TESTPOOL $disk2

#
# Wait for resilver to complete.
#
for i in {1..60}; do
	check_state $TESTPOOL "" "online" && break
	sleep 1
done

#
# Verify the pool is back to ONLINE.
#
log_must check_state $TESTPOOL "" "online"

#
# Run scrub and verify no errors.
#
log_must zpool scrub $TESTPOOL
log_must wait_scrubbed $TESTPOOL

log_pass "Offlining a disk from an anymirror1 pool with surplus disks preserves data"
