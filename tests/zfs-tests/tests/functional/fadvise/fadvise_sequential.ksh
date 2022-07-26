#!/bin/ksh -p
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
# Portions Copyright (c) 2022 Information2 Software, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib

#
# DESCRIPTION:
# Test posix_fadvise.
#
# STRATEGY:
# 1. Set primarycache to metadata in order to disable prefetch
# 2. Write some data to file 
# 3. get data_size field from arcstat
# 4. call file_fadvise with POSIX_FADV_SEQUENTIAL
# 5. get data_size field from arcstat again
# 6. latter data_size should be bigger than former one
#

# NOTE: if HAVE_FILE_FADVISE is not defined former data_size
# should less or eaqul to latter one

verify_runnable "global"

FILE=$TESTDIR/$TESTFILE0
BLKSZ=$(get_prop recordsize $TESTPOOL)

function cleanup
{
	log_must zfs set primarycache=all $TESTPOOL
	[[ -e $TESTDIR ]] && log_must rm -Rf $TESTDIR/*
}

getstat() {
	awk -v c="$1" '$1 == c {print $3; exit}' /proc/spl/kstat/zfs/arcstats
}

log_assert "Ensure fadvise prefetch data"

log_onexit cleanup

log_must zfs set primarycache=metadata $TESTPOOL

log_must file_write -o create -f $FILE -b $BLKSZ -c 1000
sync_pool $TESTPOOL

data_size1=$(getstat data_size)

log_must file_fadvise -f $FILE -a 2
sleep 10

data_size2=$(getstat data_size)
log_note "original data_size is $data_size1, final data_size is $data_size2"

log_must [ $data_size1 -le $data_size2 ]

log_pass "Ensure data could be prefetched"
