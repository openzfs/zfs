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
# Copyright (c) 2021 by The FreeBSD Foundation.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Test FALLOC_FL_ZERO_RANGE functionality
#
# STRATEGY:
# 1. Create a dense file
# 2. Zero various ranges in the file and verify the result.
#

verify_runnable "global"

if is_freebsd; then
	log_unsupported "FreeBSD does not implement an analogue to ZERO_RANGE."
fi

FILE=$TESTDIR/$TESTFILE0
BLKSZ=$(get_prop recordsize $TESTPOOL)

function cleanup
{
	[[ -e $TESTDIR ]] && log_must rm -f $FILE
}

# Helpfully, this function expects kilobytes, and check_apparent_size expects bytes.
function check_reported_size
{
	typeset expected_size=$1

	if ! [ -e "${FILE}" ]; then
		log_fail "$FILE does not exist"
	fi
		
	reported_size=$(du "${FILE}" | awk '{print $1}')
	if [ "$reported_size" != "$expected_size" ]; then
		log_fail "Incorrect reported size: $reported_size != $expected_size"
	fi
}

function check_apparent_size
{
	typeset expected_size=$1

	apparent_size=$(stat_size "${FILE}")
	if [ "$apparent_size" != "$expected_size" ]; then
		log_fail "Incorrect apparent size: $apparent_size != $expected_size"
	fi
}

log_assert "Ensure ranges can be zeroed in files"

log_onexit cleanup

# Create a dense file and check it is the correct size.
log_must file_write -o create -f $FILE -b $BLKSZ -c 8
sync_pool $TESTPOOL
log_must check_reported_size 1027

# Zero a range covering the first full block.
log_must zero_range 0 $BLKSZ $FILE
sync_pool $TESTPOOL
log_must check_reported_size 899

# Partially zero a range in the second block.
log_must zero_range $BLKSZ $((BLKSZ / 2)) $FILE
sync_pool $TESTPOOL
log_must check_reported_size 899

# Zero range which overlaps the third and fourth block.
log_must zero_range $(((BLKSZ * 2) + (BLKSZ / 2))) $((BLKSZ)) $FILE
sync_pool $TESTPOOL
log_must check_reported_size 899

# Zero range from the fifth block past the end of file, with --keep-size.
# The apparent file size must not change, since we did specify --keep-size.
apparent_size=$(stat_size $FILE)
log_must fallocate --keep-size --zero-range --offset $((BLKSZ * 4)) --length $((BLKSZ * 10)) "$FILE"
sync_pool $TESTPOOL
log_must check_reported_size 387
log_must check_apparent_size $apparent_size

# Zero range from the fifth block past the end of file.  The apparent
# file size should change since --keep-size is not implied, unlike
# with PUNCH_HOLE.
apparent_size=$(stat_size $FILE)
log_must zero_range $((BLKSZ * 4)) $((BLKSZ * 10)) $FILE
sync_pool $TESTPOOL
log_must check_reported_size 387
log_must check_apparent_size $((BLKSZ * 14))

log_pass "Ensure ranges can be zeroed in files"
