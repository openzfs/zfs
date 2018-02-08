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
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
#       Getting type should work correctly.
#

verify_runnable "global"
fs=$TESTPOOL/$TESTFS/testchild
snap=$fs@$TESTSNAP
vol=$TESTPOOL/$TESTVOL
function cleanup
{
	datasetexists $snap && log_must zfs destroy $snap
	datasetexists $fs && log_must zfs destroy $fs
	datasetexists $vol && log_must zfs destroy $vol
}

log_onexit cleanup

log_must zfs create $fs
create_snapshot $fs $TESTSNAP
log_must zfs create -V $VOLSIZE $vol

log_must_program $TESTPOOL - <<-EOF
	ans, setpoint = zfs.get_prop("$fs", "type")
	assert(ans == "filesystem")

	ans, setpoint = zfs.get_prop("$snap", "type")
	assert(ans == "snapshot")

	ans, setpoint = zfs.get_prop("$vol", "type")
	assert(ans == "volume")
EOF


log_pass "Getting type should work correctly."
