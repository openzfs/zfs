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

#
# DESCRIPTION: zfs.sync.clone() rejects clone names that zfs clone rejects
#

verify_runnable "global"

base=$TESTPOOL/$TESTFS/base

function cleanup
{
	destroy_dataset $base "-R"
}

log_onexit cleanup

log_must zfs create $base
log_must zfs create $base/fs
log_must zfs snapshot $base/fs@snap

log_must_program_sync $TESTPOOL \
    $ZCP_ROOT/synctask_core/tst.clone_neg.zcp $base/fs@snap $base

log_pass "zfs.sync.clone rejects invalid clone names"
