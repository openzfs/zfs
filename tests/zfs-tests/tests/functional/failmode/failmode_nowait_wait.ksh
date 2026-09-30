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
# Copyright (c) 2026 by the OpenZFS project.  All rights reserved.
#
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	On a pool suspended with failmode=wait, a create blocks
#	until the pool resumes instead of spinning in the kernel.
#
# STRATEGY:
#	1. Create a pool with failmode=wait and suspend it
#	2. Start a file create in the background
#	3. Its system time must stay nearly flat while it waits
#	4. Resume the pool and let the create finish
#

verify_runnable "global"

log_assert "A create on a suspended pool blocks instead of spinning"

typeset cpid="" ddpid=""

function cleanup
{
	zinject -c all
	zpool clear $TESTPOOL
	[[ -n "$cpid" ]] && wait $cpid
	[[ -n "$ddpid" ]] && wait $ddpid
	destroy_pool $TESTPOOL
}

function stime
{
	awk '{print $15}' /proc/$1/stat
}

DISK=${DISKS%% *}

log_onexit cleanup
log_must zpool create -f -o failmode=wait $TESTPOOL $DISK
log_must zfs create $TESTPOOL/$TESTFS
typeset mnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)

log_must zinject -d $DISK -e io -T write $TESTPOOL
log_must zinject -d $DISK -e nxio -T probe $TESTPOOL
dd if=/dev/urandom of=$mnt/fill bs=128k count=8 conv=fsync &
ddpid=$!
typeset -i tries=30
until [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]]; do
	((tries-- > 0)) || log_fail "pool did not suspend"
	sleep 1
done

touch $mnt/new &
cpid=$!
sleep 1
typeset -i before=$(stime $cpid)
sleep 5
typeset -i after=$(stime $cpid)
log_note "system time went from $before to $after ticks"

log_must kill -0 $cpid
log_must [ $((after - before)) -lt 50 ]

log_pass "A create on a suspended pool blocks instead of spinning"
