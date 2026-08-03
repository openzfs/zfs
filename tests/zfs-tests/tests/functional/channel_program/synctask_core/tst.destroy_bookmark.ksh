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

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

verify_runnable "global"

fs=$TESTPOOL/$TESTFS/testchild
snapname=testsnap
bookname=testbookmark
bookmark=$fs#$bookname
snap=$fs@$snapname

function cleanup
{
	destroy_dataset $fs "-R"
}

log_onexit cleanup

log_must zfs create $fs
log_must zfs snapshot $snap
log_must zfs bookmark $snap $bookmark

log_must_program_sync $TESTPOOL - $bookmark <<-EOF
	arg = ...
	bookmark = arg["argv"][1]
	err = zfs.sync.destroy(bookmark)
	msg = "destroying " .. bookmark .. " err=" .. err
	return msg
EOF

log_mustnot zfs list -H -o name -t bookmark $bookmark

log_pass "Destroying bookmark with channel program works."
