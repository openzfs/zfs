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
# Copyright 2026 Colin K. Williams / LINK ORG LLC / LI-NK.SOCIAL. All rights reserved.
#

. $STF_SUITE/tests/functional/zoned_uid/zoned_uid_common.kshlib

#
# DESCRIPTION:
#	Verify that an authorized user namespace can create snapshots
#	of datasets under the delegation root.
#
# STRATEGY:
#	1. Create a delegation root with zoned_uid set
#	2. Create a child dataset (from global zone for setup)
#	3. Enter user namespace owned by the zoned_uid
#	4. Create a snapshot from within the user namespace
#	5. Verify the snapshot was created successfully
#	6. Verify the snapshot is visible from both namespaces
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf $TESTPOOL/$TESTFS/deleg_root 2>/dev/null
}

log_assert "Authorized user namespace can create snapshots"
log_onexit cleanup

# Create delegation root and child dataset
log_must zfs create $TESTPOOL/$TESTFS/deleg_root
log_must set_zoned_uid $TESTPOOL/$TESTFS/deleg_root $ZONED_TEST_UID
log_must zfs create $TESTPOOL/$TESTFS/deleg_root/child

log_note "Delegation root created with zoned_uid=$ZONED_TEST_UID"

# Enter user namespace and create snapshot
log_note "Attempting to create snapshot from user namespace..."

typeset snap_result
snap_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs snapshot $TESTPOOL/$TESTFS/deleg_root/child@snap1 2>&1)
typeset snap_status=$?

if [[ $snap_status -ne 0 ]]; then
	log_note "Snapshot output: $snap_result"
	log_fail "Failed to create snapshot from user namespace (status=$snap_status)"
fi

log_note "Snapshot created successfully from user namespace"

# Verify snapshot exists from global zone
log_must zfs list -t snapshot $TESTPOOL/$TESTFS/deleg_root/child@snap1
log_note "Snapshot verified from global zone"

# Verify snapshot is visible from user namespace
typeset list_result
list_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs list -t snapshot $TESTPOOL/$TESTFS/deleg_root/child@snap1 2>&1)
typeset list_status=$?

if [[ $list_status -ne 0 ]]; then
	log_note "List output: $list_result"
	log_fail "Snapshot not visible from user namespace"
fi

log_note "Snapshot visible from user namespace"

# Also test snapshot of the delegation root itself
log_note "Testing snapshot of delegation root..."
typeset root_snap_result
root_snap_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs snapshot $TESTPOOL/$TESTFS/deleg_root@rootsnap 2>&1)
typeset root_snap_status=$?

if [[ $root_snap_status -ne 0 ]]; then
	log_note "Root snapshot output: $root_snap_result"
	log_fail "Failed to snapshot delegation root from user namespace"
fi

log_must zfs list -t snapshot $TESTPOOL/$TESTFS/deleg_root@rootsnap
log_note "Delegation root snapshot created successfully"

log_pass "Authorized user namespace can create snapshots"
