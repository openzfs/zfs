#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
# Copyright (c) 2021 by The FreeBSD Foundation.
# Copyright (c) 2022 by Delphix. All rights reserved.
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
# Note: We can't compare exact block numbers as reported by du, because
# different backing stores may allocate different numbers of blocks for
# the same amount of data.
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

function get_reported_size
{
	if ! [ -e "$FILE" ]; then
		log_fail "$FILE does not exist"
	fi

	sync_pool $TESTPOOL >/dev/null 2>&1
	du "$FILE" | awk '{print $1}'
}

function check_apparent_size
{
	typeset expected_size=$1

	apparent_size=$(stat_size "$FILE")
	if [ "$apparent_size" != "$expected_size" ]; then
		log_fail \
		    "Incorrect apparent size: $apparent_size != $expected_size"
	fi
}

log_assert "Ensure ranges can be zeroed in files"

log_onexit cleanup

# Create a dense file and check it is the correct size.
log_must file_write -o create -f $FILE -b $BLKSZ -c 8
sync_pool $TESTPOOL
full_size=$(get_reported_size)

# Zero a range covering the first full block. The reported size should decrease.
log_must zero_range 0 $BLKSZ $FILE
one_range=$(get_reported_size)
[[ $full_size -gt $one_range ]] || log_fail \
    "One range failure: $full_size -> $one_range"

# Partially zero a range in the second block. The reported size should
# remain constant.
log_must zero_range $BLKSZ $((BLKSZ / 2)) $FILE
partial_range=$(get_reported_size)
[[ $one_range -eq $partial_range ]] || log_fail \
    "Partial range failure: $one_range -> $partial_range"

# Zero range which overlaps the third and fourth block. The reported size
# should remain constant.
log_must zero_range $(((BLKSZ * 2) + (BLKSZ / 2))) $((BLKSZ)) $FILE
overlap_range=$(get_reported_size)
[[ $one_range -eq $overlap_range ]] || log_fail \
    "Overlap range failure: $one_range -> $overlap_range"

# Zero range from the fifth block past the end of file, with --keep-size.
# The reported size should decrease, and the apparent file size must not
# change, since we did specify --keep-size.
apparent_size=$(stat_size $FILE)
log_must fallocate --keep-size --zero-range --offset $((BLKSZ * 4)) --length $((BLKSZ * 10)) "$FILE"
eof_range=$(get_reported_size)
[[ $overlap_range -gt $eof_range ]] || log_fail \
    "EOF range failure: $overlap_range -> $eof_range"
log_must check_apparent_size $apparent_size

# Zero range from the fifth block past the end of file.  The apparent
# file size should change since --keep-size is not implied, unlike
# with PUNCH_HOLE. The reported size should remain constant.
apparent_size=$(stat_size $FILE)
log_must zero_range $((BLKSZ * 4)) $((BLKSZ * 10)) $FILE
eof_range2=$(get_reported_size)
[[ $eof_range -eq $eof_range2 ]] || log_fail \
    "Second EOF range failure: $eof_range -> $eof_range2"
log_must check_apparent_size $((BLKSZ * 14))

log_pass "Ensure ranges can be zeroed in files"
