#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Test `fallocate --punch-hole`
#
# STRATEGY:
# 1. Create a dense file
# 2. Punch an assortment of holes in the file and verify the result.
#

verify_runnable "global"

FILE=$TESTDIR/$TESTFILE0
BLKSZ=$(get_prop recordsize $TESTPOOL)

function cleanup
{
	[[ -e $TESTDIR ]] && log_must rm -f $FILE
}

function check_disk_size
{
	typeset expected_size=$1

	disk_size=$(du $TESTDIR/file | awk '{print $1}')
	if [ $disk_size -ne $expected_size ]; then
		log_fail "Incorrect size: $disk_size != $expected_size"
	fi
}

function check_apparent_size
{
	typeset expected_size=$1

	apparent_size=$(stat_size)
	if [ $apparent_size -ne $expected_size ]; then
		log_fail "Incorrect size: $apparent_size != $expected_size"
	fi
}

log_assert "Ensure holes can be punched in files making them sparse"

log_onexit cleanup

# Create a dense file and check it is the correct size.
log_must file_write -o create -f $FILE -b $BLKSZ -c 8
log_must check_disk_size  $((131072 * 8))

# Punch a hole for the first full block.
log_must fallocate --punch-hole --offset 0 --length $BLKSZ $FILE
log_must check_disk_size  $((131072 * 7))

# Partially punch a hole in the second block.
log_must fallocate --punch-hole --offset $BLKSZ --length $((BLKSZ / 2)) $FILE
log_must check_disk_size  $((131072 * 7))

# Punch a hole which overlaps the third and forth block.
log_must fallocate --punch-hole --offset $(((BLKSZ * 2) + (BLKSZ / 2))) \
    --length $((BLKSZ)) $FILE
log_must check_disk_size  $((131072 * 7))

# Punch a hole from the fifth block past the end of file.  The apparent
# file size should not change since --keep-size is implied.
apparent_size=$(stat_size $FILE)
log_must fallocate --punch-hole --offset $((BLKSZ * 4)) \
    --length $((BLKSZ * 10)) $FILE
log_must check_disk_size  $((131072 * 4))
log_must check_apparent_size $apparent_size

log_pass "Ensure holes can be punched in files making them sparse"
