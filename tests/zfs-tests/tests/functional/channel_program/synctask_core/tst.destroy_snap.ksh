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
# Copyright (c) 2016, 2017 by Delphix. All rights reserved.
#

verify_runnable "global"

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

snap=$TESTPOOL/$TESTFS@$TESTSNAP

function cleanup
{
	datasetexists $snap && log_must zfs destroy $snap
}

log_onexit cleanup

create_snapshot $TESTPOOL/$TESTFS $TESTSNAP

log_must snapexists $snap

log_must_program_sync $TESTPOOL - $snap <<-EOF
	arg = ...
	snap = arg["argv"][1]
	err = zfs.sync.destroy(snap)
	msg = "destroying " .. snap .. " err=" .. err
	return msg
EOF

log_mustnot snapexists $snap

log_pass "Destroying snapshot with channel program works."
