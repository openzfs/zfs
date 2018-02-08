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
#       Attempting to promote a clone when it shares a snapshot name with
#       its parent filesystem should fail and return the name of the
#       conflicting snapshot.
#

verify_runnable "global"

fs=$TESTPOOL/$TESTFS/testchild
clone=$TESTPOOL/$TESTFS/testchild_clone
snap=promote_conflict_snap

function cleanup
{
    for to_destroy in $fs $clone; do
        datasetexists $to_destroy && log_must zfs destroy -R $to_destroy
    done
}

log_onexit cleanup

log_must zfs create $fs
log_must zfs snapshot $fs@$snap
log_must zfs clone $fs@$snap $clone
log_must zfs snapshot $clone@$snap

#
# This channel program is expected to return successfully, but fail to execute
# the promote command since the snapshot names collide. It returns the error
# code and description, which should be EEXIST (17) and the name of the
# conflicting snapshot.
#
log_must_program $TESTPOOL \
    $ZCP_ROOT/synctask_core/tst.promote_conflict.zcp $clone

log_pass "Promoting a clone with a conflicting snapshot fails."
