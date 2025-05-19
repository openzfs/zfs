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

#
# Copyright (c) 2025 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zpool attach' works to expand mirrors and anyraid vdevs
#
# STRATEGY:
# 1. Create a normal striped pool
# 2. Verify that attaching creates a mirror
# 3. Verify that attaching again creates a wider mirror
# 4. Create an anyraid vdev
# 5. Verify that attaching expands the anyraid vdev
#

verify_runnable "global"

cleanup() {
	log_must zpool destroy $TESTPOOL2
	restore_tunable ANYRAID_MIN_TILE_SIZE
}

log_onexit cleanup

log_must truncate -s 8G /$TESTPOOL/vdev_file.{0,1,2,3}
save_tunable ANYRAID_MIN_TILE_SIZE
set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824

log_assert "'zpool attach' works to expand mirrors and anyraid vdevs"

log_must create_pool $TESTPOOL2 /$TESTPOOL/vdev_file.0
log_must zpool attach $TESTPOOL2 /$TESTPOOL/vdev_file.0 /$TESTPOOL/vdev_file.1
log_must eval "zpool list -v $TESTPOOL2 | grep \"  mirror\""
log_must eval "zpool list -v $TESTPOOL2 | grep \"    .*_file.0\""
log_must eval "zpool list -v $TESTPOOL2 | grep \"    .*_file.1\""
log_must zpool attach $TESTPOOL2 /$TESTPOOL/vdev_file.0 /$TESTPOOL/vdev_file.2
log_must eval "zpool list -v $TESTPOOL2 | grep \"    .*_file.2\""
log_must zpool destroy $TESTPOOL2

log_must create_pool $TESTPOOL2 anymirror1 /$TESTPOOL/vdev_file.{0,1,2}
log_must zpool attach $TESTPOOL2 anymirror-0 /$TESTPOOL/vdev_file.3
log_must eval "zpool list -v $TESTPOOL2 | grep \"    .*_file.3\""

log_pass "'zpool attach' works to expand mirrors and anyraid vdevs"
