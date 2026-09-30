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
# Copyright (c) 2026 Klara Systems, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that fsync() does not unduly multiply write sizes.  With
# a large `recordsize` and fsync(), repeated small writes could
# be multiplied by `recordsize` to consume a great deal of memory
# and bandwidth.
#
# STRATEGY:
#
# 1. Create a dataset with `compression=off recordsize=16MiB logbias=throughput`
# 3. Get the current TXG count of the pool
# 2. Create a 33MiB file
# 4. Append 1 byte to the end of the file, 1024 times
# 5. Get the new TXG count
# 6. Verify that enough TXGs have passed
#

verify_runnable "global"

function cleanup
{
	zfs destroy $TESTPOOL/$TESTFS1
}

log_onexit cleanup
log_assert "large recordsize does not take undue storage/bandwith with fsync()"
DISK=${DISKS%% *}

log_must zfs create -o compression=off -o recordsize=16MiB -o logbias=throughput $TESTPOOL/$TESTFS1

starting_txg=$(kstat_pool $TESTPOOL txgs | tail -n 1 | awk '{print $1}')
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS1)
log_must file_fsync -vvv --sparse-fsync 100 --path $mntpnt/test.data

ending_txg=$(kstat_pool $TESTPOOL txgs | tail -n 1 | awk '{print $1}')
let "RESULT = $ending_txg - $starting_txg"
log_must test $RESULT -gt 10

log_pass
