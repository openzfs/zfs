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
# 2. Create a 33MiB file
# 3. Examine `zpool get write_bytes fs vdev`
# 4. Append 1 byte to the end of the file, 1024 times
# 5. Examine `zpool get write_bytes fs vdev` again
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

INITIAL=$(zpool get -pH write_bytes $TESTPOOL $DISK | awk '{print $3}')
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS1)
log_must $STF_SUITE/tests/functional/fsync/many_fsync -vvv --sparse-fsync 100 --path $mntpnt/test.data

FINAL=$(zpool get -pH write_bytes $TESTPOOL $DISK | awk '{print $3}')
let "RESULT = $FINAL - $INITIAL"
log_must test $RESULT -lt $((3 * 1024 * 1024 * 1024))

log_pass
