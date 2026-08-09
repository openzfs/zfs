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
# should less or equal to latter one

verify_runnable "global"

FILE=$TESTDIR/$TESTFILE0
BLKSZ=$(get_prop recordsize $TESTPOOL)

function cleanup
{
	log_must zfs set primarycache=all $TESTPOOL
	[[ -e $TESTDIR ]] && log_must rm -Rf $TESTDIR/*
}

log_assert "Ensure fadvise prefetch data"

log_onexit cleanup

log_must zfs set primarycache=metadata $TESTPOOL

log_must file_write -o create -f $FILE -b $BLKSZ -c 1000
sync_pool $TESTPOOL

data_size1=$(kstat arcstats.data_size)

log_must file_fadvise -f $FILE -a POSIX_FADV_WILLNEED
sleep 10

data_size2=$(kstat arcstats.data_size)
log_note "original data_size is $data_size1, final data_size is $data_size2"

log_must [ $data_size1 -le $data_size2 ]

log_pass "Ensure data could be prefetched"
