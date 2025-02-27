#! /bin/ksh -p
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
