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
#       Getting index props should work correctly.
#

verify_runnable "global"

fs=$TESTPOOL/$TESTFS/testchild
snap=$fs@$TESTSNAP
function cleanup
{
	datasetexists $fs && log_must zfs destroy -R $fs
}

log_onexit cleanup

log_must zfs create -o version=5 $fs
create_snapshot $fs $TESTSNAP

log_must_program $TESTPOOL $ZCP_ROOT/synctask_core/tst.get_index_props.zcp $fs $snap

log_pass "Getting index props should work correctly."
