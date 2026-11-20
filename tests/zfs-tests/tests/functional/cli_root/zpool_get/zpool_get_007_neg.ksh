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
# Copyright (c) 2026 John White
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zpool get' and 'zpool status' on a pool that failed to load must not
# crash the kernel.
#
# STRATEGY:
# 1. Create a pool on a file vdev.
# 2. Use 'zinject -u -t mosdir' to unload the pool and to make every
#    read of the MOS object directory fail.  The pool stays in the
#    namespace, but the next open (and so its load) fails.
# 3. Run 'zpool get all' and 'zpool status'.  They can fail, but they
#    must return.
# 4. Clear the injection and check that the pool loads and is healthy.
#
# NOTES:
# A pool that failed to load stays in the namespace with no DSL pool and
# no MOS (spa_dsl_pool and spa_meta_objset are NULL).  The get-properties
# ioctl then falls back to spa_lookup() and spa_prop_get(), which took
# the DSL pool config lock without a NULL check.  On such a kernel this
# test hits a NULL pointer dereference in rrw_held(), the thread dies with
# the namespace lock held, and later zpool commands hang.
#

verify_runnable "global"

function cleanup
{
	log_must zinject -c all
	destroy_pool $TESTPOOL1
	rm -f $VDEV
}

log_assert "'zpool get' and 'zpool status' survive a pool that failed to load"
log_onexit cleanup

VDEV=$TEST_BASE_DIR/zpool_get_007_vdev
log_must truncate -s $MINVDEVSIZE $VDEV
# No mounted dataset: zinject -u exports the pool, and a mount makes it busy.
log_must zpool create -O mountpoint=none $TESTPOOL1 $VDEV
log_must zpool sync $TESTPOOL1

# Unload the pool and fail the next load.
log_must zinject -u -t mosdir $TESTPOOL1

# The load fails, so the pool is in the namespace but not usable.  The
# exit codes do not matter here: the commands must return.
zpool get all $TESTPOOL1 >/dev/null 2>&1
zpool status $TESTPOOL1 >/dev/null 2>&1
zpool get -H -o value health $TESTPOOL1 >/dev/null 2>&1
log_note "zpool get and zpool status on the unloadable pool returned"

# The pool must still respond and must load once the fault is gone.
log_must zinject -c all
log_must zpool get all $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "state" "ONLINE"

log_pass "'zpool get' and 'zpool status' survive a pool that failed to load"
