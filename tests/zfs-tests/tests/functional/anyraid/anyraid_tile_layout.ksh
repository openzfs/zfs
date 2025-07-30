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
# Anyraid disks intelligently select which tiles to use
#
# STRATEGY:
# 1. Create an anyraid1 vdev with 1 large disk and 2 small disks
# 2. Verify that the full space can be used
#

verify_runnable "global"

cleanup() {
	zpool destroy $TESTPOOL2
	zpool destroy $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
}

log_onexit cleanup

log_must create_pool $TESTPOOL $DISKS

log_must truncate --size=512M /$TESTPOOL/vdev_file.{0,1,2}
log_must truncate --size=1G /$TESTPOOL/vdev_file.3
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Anyraid disks intelligently select which tiles to use"

log_must create_pool $TESTPOOL2 anyraid1 /$TESTPOOL/vdev_file.{0,1,2,3}

cap=$(zpool get -Hp -o value size $TESTPOOL2)
[[ "$cap" -eq $((9 * 64 * 1024 * 1024)) ]] || \
	log_fail "Incorrect space for anyraid vdev: $cap"

#
# This should just about fill the pool, when you account for the 128MiB of
# reserved slop space. If the space isn't being selected intelligently, we
# would hit ENOSPC 64MiB early.
#
log_must dd if=/dev/urandom of=/$TESTPOOL2/f1 bs=1M count=$((64 * 7 - 1))

log_pass "Anyraid disks intelligently select which tiles to use"
