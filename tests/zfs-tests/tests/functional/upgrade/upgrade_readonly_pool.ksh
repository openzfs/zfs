#!/bin/ksh -p
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
# Copyright 2019, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/tests/functional/upgrade/upgrade_common.kshlib

#
# DESCRIPTION:
# User accounting upgrade should not be executed on readonly pool
#
# STRATEGY:
# 1. Create a pool with the feature@userobj_accounting disabled to simulate
#    a legacy pool from a previous ZFS version.
# 2. Create a file on the "legecy" dataset and store its checksum
# 3. Enable feature@userobj_accounting on the pool and verify it is only
#    "enabled" and not "active": upgrading starts when the filesystem is mounted
# 4. Export the pool and re-import is readonly, without mounting any filesystem
# 5. Try to mount the root dataset manually without the "ro" option, then verify
#    filesystem status and the pool feature status (not "active") to ensure the
#    pool "readonly" status is enforced.
#

verify_runnable "global"

TESTFILE="$TESTDIR/file.bin"

log_assert "User accounting upgrade should not be executed on readonly pool"
log_onexit cleanup_upgrade

# 1. Create a pool with the feature@userobj_accounting disabled to simulate
#    a legacy pool from a previous ZFS version.
log_must zpool create -d -m $TESTDIR $TESTPOOL $TMPDEV

# 2. Create a file on the "legecy" dataset
log_must touch $TESTDIR/file.bin

# 3. Enable feature@userobj_accounting on the pool and verify it is only
#    "enabled" and not "active": upgrading starts when the filesystem is mounted
log_must zpool set feature@userobj_accounting=enabled $TESTPOOL
log_must test "enabled" == "$(get_pool_prop 'feature@userobj_accounting' $TESTPOOL)"

# 4. Export the pool and re-import is readonly, without mounting any filesystem
log_must zpool export $TESTPOOL
log_must zpool import -o readonly=on -N -d "$(dirname $TMPDEV)" $TESTPOOL

# 5. Try to mount the root dataset manually without the "ro" option, then verify
#    filesystem status and the pool feature status (not "active") to ensure the
#    pool "readonly" status is enforced.
log_must mount -t zfs -o zfsutil $TESTPOOL $TESTDIR
log_must stat "$TESTFILE"
log_mustnot touch "$TESTFILE"
log_must test "enabled" == "$(get_pool_prop 'feature@userobj_accounting' $TESTPOOL)"

log_pass "User accounting upgrade is not executed on readonly pool"
