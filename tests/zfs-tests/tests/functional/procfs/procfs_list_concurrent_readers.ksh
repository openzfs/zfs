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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Make sure that interleaving reads from different readers does not affect the
# results that are returned.
#
# STRATEGY:
# 1. Make sure a few debug messages have been logged.
# 2. Open the procfs file and start reading from it.
# 3. Open the file again, and read its entire contents.
# 4. Resume reading from the first instance.
# 5. Check that the contents read by the two instances are identical.
#

function cleanup
{
	log_must rm -f $msgs1 $msgs2
	datasetexists $FS && destroy_dataset $FS -r
}

typeset -r ZFS_DBGMSG=/proc/spl/kstat/zfs/dbgmsg
typeset -r FS=$TESTPOOL/fs
typeset msgs1 msgs2

log_onexit cleanup

# Clear out old messages
echo 0 >$ZFS_DBGMSG || log_fail "failed to write to $ZFS_DBGMSG"

# Add some new messages
log_must zfs create $FS
for i in {1..20}; do
	log_must zfs snapshot "$FS@testsnapshot$i"
done
sync_pool $TESTPOOL

msgs1=$(mktemp) || log_fail
msgs2=$(mktemp) || log_fail

#
# Start reading file, pause and read it from another process, and then finish
# reading.
#
{ dd bs=512 count=4; cp $ZFS_DBGMSG $msgs1; cat; } <$ZFS_DBGMSG >$msgs2

#
# Truncate the result of the read that completed second in case it picked up an
# extra message that was logged after the first read completed.
#
log_must truncate -s $(stat_size $msgs1) $msgs2

log_must diff $msgs1 $msgs2

log_pass "Concurrent readers receive identical results"
