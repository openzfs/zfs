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

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

verify_runnable "global"
snap1=$TESTPOOL/$TESTFS@$TESTSNAP1
snap2=$TESTPOOL/$TESTFS@$TESTSNAP2
fs=$TESTPOOL/$TESTFS
file=$TESTDIR/$TESTFILE0

function cleanup
{
	datasetexists $snap1 && log_must zfs destroy $snap1 && \
	    log_must rm $file
}

log_onexit cleanup

log_must mkfile 128b $file
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP1
log_must rm $file
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP2

log_must snapexists $snap1
log_must snapexists $snap2
log_must zfs unmount $fs

log_must_program $TESTPOOL - $fs $snap2 <<-EOF
	arg = ...
	fs = arg["argv"][1]
	snap = arg["argv"][2]
	err = zfs.sync.rollback(fs)
	if err == 0 then
		err = zfs.sync.destroy(snap)
	end
	if err == 0 then
		err = zfs.sync.rollback(fs)
	end
	msg = "rolling back " .. fs .. " err=" .. err
	return msg
EOF

log_must zfs mount $fs
log_must [ -f $file ]
log_mustnot snapexists $snap2

log_pass "Rolling back snapshot with channel program works."
