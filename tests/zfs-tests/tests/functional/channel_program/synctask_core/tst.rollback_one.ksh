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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2016, 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

verify_runnable "global"
snap=$TESTPOOL/$TESTFS@$TESTSNAP
fs=$TESTPOOL/$TESTFS
file=$TESTDIR/$TESTFILE0

function cleanup
{
	destroy_dataset $snap && log_must rm $file
}

log_onexit cleanup

log_must mkfile 128b $file
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP
log_must rm $file

log_must snapexists $snap
log_must zfs unmount $fs

log_must_program_sync $TESTPOOL - $fs <<-EOF
	arg = ...
	fs = arg["argv"][1]
	err = zfs.sync.rollback(fs)
	msg = "rolling back " .. fs .. " err=" .. err
	return msg
EOF

log_must zfs mount $fs
log_must [ -f $file ]

log_pass "Rolling back snapshot with channel program works."
