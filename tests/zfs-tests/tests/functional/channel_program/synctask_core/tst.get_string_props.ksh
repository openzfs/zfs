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
#       Getting string props should work correctly.
#

verify_runnable "global"

fs=$TESTPOOL/testchild
snap=$fs@$TESTSNAP
clone=$TESTPOOL/$TESTCLONE


function cleanup
{
	datasetexists $clone && destroy_dataset $clone
	datasetexists $fs && destroy_dataset $fs -R
}

log_onexit cleanup

log_must zfs create $fs
create_snapshot $fs $TESTSNAP
create_clone $snap $clone

log_must_program $TESTPOOL $ZCP_ROOT/synctask_core/tst.get_string_props.zcp $fs $snap $clone

log_pass "Getting string props should work correctly."
