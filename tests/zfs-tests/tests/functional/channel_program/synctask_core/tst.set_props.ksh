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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION:
#	Setting user props should work correctly on datasets.
#

verify_runnable "global"

fs=$TESTPOOL/$TESTFS/testchild

function cleanup
{
	destroy_dataset $fs "-R"
}

log_onexit cleanup

log_must zfs create $fs

log_must_program_sync $TESTPOOL $ZCP_ROOT/synctask_core/tst.set_props.zcp $fs

log_pass "Setting props from channel program works correctly."
