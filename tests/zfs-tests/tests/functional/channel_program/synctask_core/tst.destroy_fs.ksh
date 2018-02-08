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

fs=$TESTPOOL/$TESTFS/testchild

function cleanup
{
	datasetexists $fs && log_must zfs destroy $fs
}

log_onexit cleanup

log_must zfs create $fs
log_must zfs unmount $fs

log_must datasetexists $fs

log_must_program_sync $TESTPOOL - $fs <<-EOF
	arg = ...
	fs = arg["argv"][1]
	err = zfs.sync.destroy(fs)
	msg = "destroying " .. fs .. " err=" .. err
	return msg
EOF

log_mustnot datasetexists $fs

log_pass "Destroying filesystem with channel program works."
