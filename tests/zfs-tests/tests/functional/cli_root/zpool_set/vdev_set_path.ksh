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
#	Setting the "path" vdev property completes and takes effect.
#
#	Like "allocating", the "path" property re-takes SCL_CONFIG as a
#	writer underneath vdev_prop_set(): it calls spa_vdev_setpath() ->
#	spa_vdev_set_common() -> spa_vdev_state_enter(spa, SCL_ALL) ->
#	spa_config_enter(spa, SCL_ALL, RW_WRITER).  When
#	zfs_ioc_vdev_set_props() holds SCL_CONFIG as a reader across
#	vdev_prop_set(), taking it as a writer while the same thread already
#	holds it as a reader self-deadlocks the calling thread, so "zpool set
#	path=..." hangs in spa_config_enter().
#
# STRATEGY:
#	1. Create a pool on our own file vdev (so we don't collide with the
#	   pool the zpool_set setup creates on $DISKS).
#	2. Point a /dev symlink at the leaf and set path= to it; verify the
#	   command returns and the property reads back the new path.
#
#	"path" only records the string and requires a /dev/ prefix; it does
#	not reopen the device, so a /dev symlink standing in for the file
#	vdev is sufficient to exercise the set path.
#

verify_runnable "global"

typeset POOL=path_testpool
typeset VDEV=$TEST_BASE_DIR/vdev_path.$$
typeset SYMLINK=/dev/vdev_set_path_$$

function cleanup
{
	poolexists $POOL && destroy_pool $POOL
	rm -f $VDEV $SYMLINK
}

log_onexit cleanup

log_assert "setting the path vdev property completes and takes effect"

log_must truncate -s $MINVDEVSIZE $VDEV
log_must zpool create $POOL $VDEV

# The stored leaf path is the file vdev we created.
typeset OLDPATH=$(zpool get -H -o value path $POOL $VDEV)
log_must test -n "$OLDPATH"

# A new /dev path for the leaf; "path" must start with /dev/.
log_must ln -s $OLDPATH $SYMLINK

# The operation that previously deadlocked.
log_must zpool set path=$SYMLINK $POOL $OLDPATH

# The single leaf is the only vdev with a path; confirm it now reads back
# the new value.  (Address by "all-vdevs" rather than by the new path, to
# avoid depending on how the CLI re-resolves a just-renamed vdev.)
log_must test \
    "$(zpool get -H -o value path $POOL all-vdevs | grep '^/dev/')" = \
    "$SYMLINK"

log_pass "setting the path vdev property completes and takes effect"
