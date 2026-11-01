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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	mountpoint values containing '..' path components are rejected,
#	both at "zfs set" and "zfs create -o" time.
#

verify_runnable "global"

TESTFS=$TESTPOOL/$TESTFS
DS=$TESTFS/mntdotdot
BAD="/foo/../bar"

function cleanup
{
	datasetexists $DS && destroy_dataset $DS -r
}

log_onexit cleanup

log_must zfs create $DS
MNTPT=$(get_prop mountpoint $DS)

log_mustnot zfs set mountpoint=$BAD $DS
log_must test "$(get_prop mountpoint $DS)" = "$MNTPT"

log_mustnot zfs create -o mountpoint=$BAD $DS/child
log_mustnot datasetexists $DS/child

log_pass "mountpoint rejects '..' path components."
