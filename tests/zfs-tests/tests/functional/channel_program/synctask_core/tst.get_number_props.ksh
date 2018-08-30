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

#
# DESCRIPTION:
#       Getting number props should work correctly on filesystems,
#	snapshots and volumes.
#

verify_runnable "global"

fs=$TESTPOOL/$TESTFS/testchild
snap=$fs@$TESTSNAP
vol=$TESTPOOL/$TESTVOL

function cleanup
{
	destroy_dataset $fs "-R"
	destroy_dataset $vol
}

log_onexit cleanup

log_must zfs create $fs
create_snapshot $fs $TESTSNAP
log_must zfs create -V $VOLSIZE $TESTPOOL/$TESTVOL

#
# Set snapshot_limit and filesystem_limit for the filesystem so that the
# snapshot_count and filesystem_count properties return a value.
#
log_must zfs set snapshot_limit=10 filesystem_limit=10 $fs
log_must zfs set snapshot_limit=10 $vol

log_must_program $TESTPOOL $ZCP_ROOT/synctask_core/tst.get_number_props.zcp $fs $snap $vol

log_pass "Getting number props should work correctly."
