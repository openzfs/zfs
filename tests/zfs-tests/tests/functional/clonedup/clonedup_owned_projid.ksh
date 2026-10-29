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
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib

#
# DESCRIPTION:
#	A received file can carry a project id into a dataset that
#	keeps no project usage.  A run that owns the dataset reads the
#	id only where the dataset keeps project usage, since a project
#	quota check would otherwise start the upgrade that dirties
#	every object of the dataset.
#
# STRATEGY:
#	1. In a second pool, give a file and a byte copy of it a
#	   project id, and receive them unmounted into a pool created
#	   without project_quota.  Destroy the received snapshot, so
#	   that no snapshot holds their blocks.
#	2. Enable project_quota and import the pool again without
#	   mounting anything, so the dataset opens ready for project
#	   usage it does not keep yet.
#	3. Run clonedup with a pause of a second before each block,
#	   so that it owns the dataset for seconds after it checks the
#	   first one.  It redirects the copy.
#	4. Check that the dataset still keeps no project usage, and
#	   that feature@project_quota is still enabled, not active.
#

verify_runnable "global"

typeset vdev=$TEST_BASE_DIR/clonedup_projid_vdev

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -f $vdev
	clonedup_cleanup
}

# Whether ds keeps project usage.  zfs projectspace lists nothing
# for a dataset that keeps none.
function cd_projectspace # ds
{
	zfs projectspace -o used $1 2>/dev/null | grep -q USED
}

log_assert "an owned file's project id starts no quota upgrade"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY

log_must truncate -s $MINVDEVSIZE $vdev
log_must zpool create -f -O recordsize=128k -O compression=off \
    $TESTPOOL1 $vdev
log_must zfs create $TESTPOOL1/fs
clonedup_write /$TESTPOOL1/fs/a
clonedup_dup /$TESTPOOL1/fs/a /$TESTPOOL1/fs/b
log_must zfs project -p 100 /$TESTPOOL1/fs/a /$TESTPOOL1/fs/b
log_must zfs snapshot $TESTPOOL1/fs@s

clonedup_pool_create -o feature@project_quota=disabled
log_must eval "zfs send $TESTPOOL1/fs@s |" \
    "zfs receive -u -o canmount=off $TESTPOOL/dst"
# no snapshot holds the blocks the run is to redirect
log_must zfs destroy $TESTPOOL/dst@s
log_must zpool set feature@project_quota=enabled $TESTPOOL
log_must zpool export $TESTPOOL
log_must zpool import -N $TESTPOOL
cd_projectspace $TESTPOOL/dst &&
    log_fail "$TESTPOOL/dst keeps project usage before the run"
log_must [ "$(get_pool_prop feature@project_quota $TESTPOOL)" = \
    "enabled" ]

# A second before each block keeps the dataset owned for seconds
# after the first block's checks, time enough for an upgrade task to
# run before the disown would stop it.
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 1000
clonedup_run
clonedup_sync
clonedup_jstat_is errors 0
clonedup_jstat_is clonedup_applied $CD_BLOCKS
clonedup_kstat_gt dst_owned 0
clonedup_check_shared $TESTPOOL/dst a $TESTPOOL/dst b \
    "$(clonedup_all_blocks)"
cd_projectspace $TESTPOOL/dst &&
    log_fail "the run upgraded $TESTPOOL/dst for project usage"
# an upgrade activates the feature as soon as it dirties an object
log_must [ "$(get_pool_prop feature@project_quota $TESTPOOL)" = \
    "enabled" ]
clonedup_leakcheck

log_pass "an owned file's project id starts no quota upgrade"
