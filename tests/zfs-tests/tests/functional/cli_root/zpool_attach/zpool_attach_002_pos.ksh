#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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

log_must truncate --size=8G /$TESTPOOL/vdev_file.{0,1,2,3}
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

log_must create_pool $TESTPOOL2 anyraid1 /$TESTPOOL/vdev_file.{0,1,2}
log_must zpool attach $TESTPOOL2 anyraid-0 /$TESTPOOL/vdev_file.3
log_must eval "zpool list -v $TESTPOOL2 | grep \"    .*_file.3\""

log_pass "'zpool attach' works to expand mirrors and anyraid vdevs"
