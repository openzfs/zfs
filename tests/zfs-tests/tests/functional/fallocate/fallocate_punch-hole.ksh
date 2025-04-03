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
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
# Copyright (c) 2021 by The FreeBSD Foundation.
# Copyright (c) 2022 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Test hole-punching functionality
#
# STRATEGY:
# 1. Create a dense file
# 2. Punch an assortment of holes in the file and verify the result.
#
# Note: We can't compare exact block numbers as reported by du, because
# different backing stores may allocate different numbers of blocks for
# the same amount of data.
#

verify_runnable "global"

#
# Prior to __FreeBSD_version 1400032 there are no mechanism to punch hole in a
# file on FreeBSD.  truncate -d support is required to call fspacectl(2) on
# behalf of the script.
#
if is_freebsd; then
	if [[ $(uname -K) -lt 1400032 ]]; then
		log_unsupported "Requires fspacectl(2) support on FreeBSD"
	fi
	if truncate -d 2>&1 | grep "illegal option" > /dev/null; then
		log_unsupported "Requires truncate(1) -d support on FreeBSD"
	fi
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

log_assert "Ensure holes can be punched in files making them sparse"

log_onexit cleanup

# Create a dense file and check it is the correct size.
log_must file_write -o create -f $FILE -b $BLKSZ -c 8
full_size=$(get_reported_size)

# Punch a hole for the first full block. The reported size should decrease.
log_must punch_hole 0 $BLKSZ $FILE
one_hole=$(get_reported_size)
[[ $full_size -gt $one_hole ]] || log_fail \
    "One hole failure: $full_size -> $one_hole"

# Partially punch a hole in the second block. The reported size should
# remain constant.
log_must punch_hole $BLKSZ $((BLKSZ / 2)) $FILE
partial_hole=$(get_reported_size)
[[ $one_hole -eq $partial_hole ]] || log_fail \
    "Partial hole failure: $one_hole -> $partial_hole"

# Punch a hole which overlaps the third and fourth block. The reported size
# should remain constant.
log_must punch_hole $(((BLKSZ * 2) + (BLKSZ / 2))) $((BLKSZ)) $FILE
overlap_hole=$(get_reported_size)
[[ $one_hole -eq $overlap_hole ]] || log_fail \
    "Overlap hole failure: $one_hole -> $overlap_hole"

# Punch a hole from the fifth block past the end of file.  The reported size
# should decrease, and the apparent file size should not change since
# --keep-size is implied.
apparent_size=$(stat_size $FILE)
log_must punch_hole $((BLKSZ * 4)) $((BLKSZ * 10)) $FILE
eof_hole=$(get_reported_size)
[[ $overlap_hole -gt $eof_hole ]] || log_fail \
    "EOF hole failure: $overlap_hole -> $eof_hole"
log_must check_apparent_size $apparent_size

log_pass "Ensure holes can be punched in files making them sparse"
