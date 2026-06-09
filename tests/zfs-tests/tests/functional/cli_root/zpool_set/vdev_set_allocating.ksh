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
# Copyright (c) 2026 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Toggling the "allocating" vdev property completes and takes effect.
#
#	When zfs_ioc_vdev_set_props() holds SCL_CONFIG as a reader across
#	vdev_prop_set(), setting "allocating" re-enters
#	spa_config_enter(spa, SCL_ALL, RW_WRITER) via
#	spa_vdev_noalloc()/spa_vdev_alloc(); taking SCL_CONFIG as a writer
#	while the same thread already holds it as a reader self-deadlocks the
#	calling thread, so "zpool set allocating=off" hangs in
#	spa_config_enter().
#
# STRATEGY:
#	1. Create a pool with two top-level vdevs (so one can stop allocating
#	   while a normal vdev remains).
#	2. Set allocating=off on the first vdev; verify the command returns and
#	   the property reads back "off".
#	3. Set allocating=on; verify it reads back "on".
#

verify_runnable "global"

typeset POOL=alloc_testpool
typeset VDEV0=$TEST_BASE_DIR/vdev_alloc0.$$
typeset VDEV1=$TEST_BASE_DIR/vdev_alloc1.$$

function cleanup
{
	poolexists $POOL && destroy_pool $POOL
	rm -f $VDEV0 $VDEV1
}

log_onexit cleanup

log_assert "toggling the allocating vdev property completes and takes effect"

log_must truncate -s $MINVDEVSIZE $VDEV0 $VDEV1
log_must zpool create $POOL $VDEV0 $VDEV1

# Both top-level vdevs allocate by default.
log_must test "$(zpool get -H -o value allocating $POOL $VDEV0)" = "on"

# The operation that previously deadlocked.
log_must zpool set allocating=off $POOL $VDEV0
log_must test "$(zpool get -H -o value allocating $POOL $VDEV0)" = "off"

# And back on.
log_must zpool set allocating=on $POOL $VDEV0
log_must test "$(zpool get -H -o value allocating $POOL $VDEV0)" = "on"

log_pass "toggling the allocating vdev property completes and takes effect"
