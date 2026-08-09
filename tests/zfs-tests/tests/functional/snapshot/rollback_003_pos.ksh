#! /bin/ksh -p
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_rollback/zfs_rollback_common.kshlib
. $STF_SUITE/tests/functional/snapshot/snapshot.cfg


#
# DESCRIPTION:
# Verify that rollbacks succeed when there are nested file systems.
#
# STRATEGY:
# 1) Snapshot an empty file system and rollback
# 2) Create a file in the file system
# 3) Rollback the file system to empty
# 4) Create a nested file system with the same name as the file created in (2)
# 5) Verify a rollback succeeds
#

verify_runnable "both"

function cleanup
{
	typeset snap=""
	typeset fs=""

	export __ZFS_POOL_RESTRICT="$TESTPOOL"
	log_must zfs mount -a
	unset __ZFS_POOL_RESTRICT

	for snap in "$SNAPPOOL.1" "$SNAPPOOL"; do
		if snapexists $snap; then
			destroy_snapshot $snap
		fi
	done

	for fs in "$TESTPOOL/$TESTFILE/$TESTFILE.1" "$TESTPOOL/$TESTFILE"; do
		if datasetexists $fs; then
			destroy_dataset $fs -r
		fi
	done

	[[ -e /$TESTPOOL ]] && \
		log_must rm -rf $TESTPOOL/*
}

log_assert "Verify rollback succeeds when there are nested file systems."

log_onexit cleanup

log_must zfs snapshot $SNAPPOOL
log_must zfs rollback $SNAPPOOL
log_mustnot zfs snapshot $SNAPPOOL

log_must touch /$TESTPOOL/$TESTFILE
sync_pool $TESTPOOL

log_must zfs rollback $SNAPPOOL
log_must zfs create $TESTPOOL/$TESTFILE

log_must zfs rollback $SNAPPOOL

log_note "Verify rollback of multiple nested file systems succeeds."
log_must zfs snapshot $TESTPOOL/$TESTFILE@$TESTSNAP
log_must zfs snapshot $SNAPPOOL.1

#
# Linux: Issuing a `df` seems to properly force any negative dcache entries to
# be invalidated preventing failures when accessing the mount point. Additional
# investigation required.
#
# https://github.com/openzfs/zfs/issues/6143
#
log_must eval "df >/dev/null"

export __ZFS_POOL_RESTRICT="$TESTPOOL"
log_must zfs unmount -a
log_must zfs mount -a
unset __ZFS_POOL_RESTRICT

log_must touch /$TESTPOOL/$TESTFILE/$TESTFILE.1

log_must zfs rollback $SNAPPOOL.1
log_must eval "df >/dev/null"

log_pass "Rollbacks succeed when nested file systems are present."
