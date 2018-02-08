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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       "Getting {user,group}{quota,used}, should work correctly."
#

verify_runnable "global"

fs=$TESTPOOL/$TESTFS/testchild
fs1=$TESTPOOL/$TESTFS/nextchild
userid='123'
groupid='456'

function cleanup
{
	datasetexists $fs && log_must zfs destroy $fs
	datasetexists $fs1 && log_must zfs destroy $fs1
}

log_onexit cleanup

log_must zfs create -o userquota@$userid=$UQUOTA_SIZE \
	-o groupquota@$groupid=$GQUOTA_SIZE $fs

log_must_program $TESTPOOL - <<-EOF
	ans, setpoint = zfs.get_prop("$fs", "userquota@$userid")
	assert(ans == $UQUOTA_SIZE)
	assert(setpoint == "$fs")

	ans, setpoint = zfs.get_prop("$fs", "userused@$userid")
	assert(ans == 0)
	assert(setpoint == "$fs")

	ans, setpoint = zfs.get_prop("$fs", "groupquota@$groupid")
	assert(ans == $GQUOTA_SIZE)
	assert(setpoint == "$fs")

	ans, setpoint = zfs.get_prop("$fs", "groupused@$groupid")
	assert(ans == 0)
	assert(setpoint == "$fs")
EOF

log_must zfs create $fs1
log_must_program $TESTPOOL - <<-EOF
	ans, setpoint = zfs.get_prop("$fs1", "userquota@$userid")
	assert(ans == nil)
	assert(setpoint == nil)

	ans, setpoint = zfs.get_prop("$fs1", "userused@$userid")
	assert(ans == 0)
	assert(setpoint == "$fs1")

	ans, setpoint = zfs.get_prop("$fs1", "groupquota@$groupid")
	assert(ans == nil)
	assert(setpoint == nil)

	ans, setpoint = zfs.get_prop("$fs1", "groupused@$groupid")
	assert(ans == 0)
	assert(setpoint == "$fs1")
EOF

log_pass "Getting {user,group}{quota,used}, should work correctly."
