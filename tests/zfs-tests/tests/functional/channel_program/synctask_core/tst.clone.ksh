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
# Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION: Make sure basic cloning functionality works in channel programs
#

verify_runnable "global"

base=$TESTPOOL/$TESTFS/base

function cleanup
{
	destroy_dataset $base "-R"
}

log_onexit cleanup

log_must zfs create $base

# test filesystem cloning
log_must zfs create $base/fs
log_must zfs snapshot $base/fs@snap

log_must_program_sync $TESTPOOL \
    $ZCP_ROOT/synctask_core/tst.clone.zcp $base/fs@snap $base/newfs

# test zvol cloning
log_must zfs create -s -V 100G $base/vol
log_must zfs snapshot $base/vol@snap

log_must_program_sync $TESTPOOL \
    $ZCP_ROOT/synctask_core/tst.clone.zcp $base/vol@snap $base/newvol

# make sure the dev node was created
block_device_wait $ZVOL_DEVDIR/$base/newvol

log_pass "Cloning works"
