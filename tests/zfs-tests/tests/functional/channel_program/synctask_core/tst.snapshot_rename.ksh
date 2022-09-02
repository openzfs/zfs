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
# Copyright (c) 2022 by Andriy Gapon. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION: Make sure basic snapshot functionality works in channel programs
#

verify_runnable "global"

fs=$TESTPOOL/$TESTFS/testchild
snapname1=testsnap1
snapname2=testsnap2

function cleanup
{
	destroy_dataset $fs "-R"
}

log_onexit cleanup

log_must zfs create $fs

log_must_program_sync $TESTPOOL \
    $ZCP_ROOT/synctask_core/tst.snapshot_rename.zcp $fs $snapname1 $snapname2

log_pass "Snapshot renaming works"
