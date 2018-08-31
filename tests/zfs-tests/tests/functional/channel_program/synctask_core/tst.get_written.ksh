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
#       Getting written@ props should work correctly.
#

verify_runnable "global"

fs=$TESTPOOL/testchild
snap=$fs@$TESTSNAP
dir=/$fs/dir

function cleanup
{
	destroy_dataset $fs "-R"
	log_must rm -rf $dir
}

log_onexit cleanup

log_must zfs create $fs
create_snapshot $fs $TESTSNAP

log_must_program $TESTPOOL - <<-EOF
	ans, setpoint = zfs.get_prop("$fs", "written@$TESTSNAP")
	assert(ans == 0)

EOF

log_must mkdir $dir
sync

log_must_program $TESTPOOL - <<-EOF
	ans, setpoint = zfs.get_prop("$fs", "written@$TESTSNAP")
	assert(ans > 0)

EOF

log_pass "Getting written@ props should work correctly."
