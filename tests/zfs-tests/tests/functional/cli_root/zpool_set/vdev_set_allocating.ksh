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
