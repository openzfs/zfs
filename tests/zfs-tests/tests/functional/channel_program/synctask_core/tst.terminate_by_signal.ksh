#!/bin/ksh -p
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2017 by Delphix. All rights reserved.
#
. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION: Execute a long-running zfs channel program and attempt to
# cancel it by sending a signal.
#

verify_runnable "global"

rootfs=$TESTPOOL/$TESTFS
snapname=snap
limit=50000000

function cleanup
{
	datasetexists $rootfs && destroy_dataset $rootfs -R
}

log_onexit cleanup

#
# Create a working set of 100 file systems
#
for i in {1..100}; do
	log_must zfs create "$rootfs/child$i"
done

#
# Attempt to create 100 snapshots with zfs.sync.snapshot() along with some
# time consuming efforts. We use loops of zfs.check.* (dry run operations)
# to consume instructions before the next zfs.sync.snapshot() occurs.
#
# Without a signal interruption this ZCP would take several minutes and
# generate over 30 million Lua instructions.
#
function chan_prog
{
zfs program -t $limit $TESTPOOL - $rootfs $snapname <<-EOF
	arg = ...
	fs = arg["argv"][1]
	snap = arg["argv"][2]
	for child in zfs.list.children(fs) do
		local snapname = child .. "@" .. snap
		zfs.check.snapshot(snapname)
		zfs.sync.snapshot(snapname)
		for i=1,20000,1 do
			zfs.check.snapshot(snapname)
			zfs.check.destroy(snapname)
			zfs.check.destroy(fs)
		end
	end
	return "should not have reached here"
EOF
}

log_note "Executing a long-running zfs program in the background"
chan_prog &
CHILD=$!

#
# After waiting, send a kill signal to the channel program process.
# This should stop the ZCP near a million instructions but still have
# created some of the snapshots. Note that since the above zfs program
# command might get wrapped, we also issue a kill to the group.
#
sleep 10
log_pos pkill -P $CHILD
log_pos kill $CHILD

#
# Make sure the channel program did not fully complete by enforcing
# that not all of the snapshots were created.
#
snap_count=$(zfs list -t snapshot | grep -c $TESTPOOL)
log_note "$snap_count snapshots created by ZCP"

log_mustnot [ "$snap_count" -eq 0 ]
log_mustnot [ "$snap_count" -gt 90 ]

log_pass "Cancelling a long-running channel program works."
