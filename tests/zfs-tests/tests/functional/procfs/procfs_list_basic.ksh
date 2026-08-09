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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Test that we can read from and write to a file in procfs whose contents is
# backed by a linked list.
#
# STRATEGY:
# 1. Take some snapshots of a filesystem, which will cause some messages to be
#    written to the zfs dbgmsgs.
# 2. Read the dbgmsgs via procfs and verify that the expected messages are
#    present.
# 3. Write to the dbgmsgs file to clear the messages.
# 4. Read the dbgmsgs again, and make sure the messages are no longer present.
#

function cleanup
{
	datasetexists $FS && destroy_dataset $FS -r
}

function count_snap_cmds
{
	typeset expected_count=$1
	count=$(grep -cE "command: (lt-)?zfs snapshot $FS@testsnapshot")
	log_must [ "$count" -eq "$expected_count" ]
}

typeset -r ZFS_DBGMSG=/proc/spl/kstat/zfs/dbgmsg
typeset -r FS=$TESTPOOL/fs
typeset snap_msgs

log_onexit cleanup

# Clear out old messages
echo 0 >$ZFS_DBGMSG || log_fail "failed to write to $ZFS_DBGMSG"

log_must zfs create $FS
for i in {1..20}; do
	log_must zfs snapshot "$FS@testsnapshot$i"
done
sync_pool $TESTPOOL

#
# Read the debug message file in small chunks to make sure that the read is
# split up into multiple syscalls. This tests that when a syscall begins we
# correctly pick up in the list of messages where the previous syscall left
# off. The size of the read can affect how many bytes the seq_file code has
# left in its internal buffer, which in turn can affect the relative pos that
# the seq_file code picks up at when the next read starts. Try a few
# different size reads to make sure we can handle each case.
#
# Check that the file has the right contents by grepping for some of the
# messages that we expect to be present.
#
for chunk_sz in {1,64,256,1024,4096}; do
	dd if=$ZFS_DBGMSG bs=$chunk_sz | count_snap_cmds 20
done

# Clear out old messages and check that they really are gone
echo 0 >$ZFS_DBGMSG || log_fail "failed to write to $ZFS_DBGMSG"
count_snap_cmds 0 < $ZFS_DBGMSG
#
# Even though we don't expect any messages in the file, reading should still
# succeed.
#
log_must cat $ZFS_DBGMSG

log_pass "Basic reading/writing of procfs file backed by linked list successful"
