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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Test that POSIX_FADV_DONTNEED evicts data from the ZFS dbuf cache.
#
# STRATEGY:
# 1. Write blocks to a file and sync, so they land in the dbuf LRU cache
# 2. Record cache_count from dbufstats
# 3. Call file_fadvise with POSIX_FADV_DONTNEED on the file
# 4. Verify that cache_count decreased
#

verify_runnable "global"

FILE=$TESTDIR/$TESTFILE0
BLKSZ=$(get_prop recordsize $TESTPOOL)

function cleanup
{
	[[ -e $TESTDIR ]] && log_must rm -Rf $TESTDIR/*
}

log_assert "Ensure POSIX_FADV_DONTNEED evicts data from the dbuf cache"

log_onexit cleanup

log_must file_write -o create -f $FILE -b $BLKSZ -c 100
sync_pool $TESTPOOL

evicts1=$(kstat dbufstats.cache_count)

log_must file_fadvise -f $FILE -a POSIX_FADV_DONTNEED

evicts2=$(kstat dbufstats.cache_count)
log_note "cache_count before=$evicts1 after=$evicts2"

log_must [ $evicts1 -gt $evicts2 ]

log_pass "POSIX_FADV_DONTNEED evicts data from the dbuf cache"
