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
# Copyright 2026 Colin K. Williams / LINK ORG LLC / LI-NK.SOCIAL. All rights reserved.
#

. $STF_SUITE/tests/functional/zoned_uid/zoned_uid_common.kshlib

#
# DESCRIPTION:
#	Verify that an authorized user namespace can destroy child datasets
#	and snapshots, but cannot destroy the delegation root itself.
#
# STRATEGY:
#	1. Create a delegation root with zoned_uid set
#	2. Create child datasets and snapshots
#	3. Enter user namespace and destroy a snapshot (should succeed)
#	4. Destroy a child dataset (should succeed)
#	5. Attempt to destroy the delegation root (should fail - protected)
#	6. Verify the delegation root still exists
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Authorized user namespace can destroy children but not delegation root"
log_onexit cleanup

# Create delegation root with children
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    destroy,mount
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/child1"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/child2"
log_must zfs snapshot "$TESTPOOL/$TESTFS/deleg_root/child1@snap1"

log_note "Created delegation root with children and snapshot"

# Unmount child datasets from global zone before entering user namespace.
# Mounts inherited from the parent mount namespace are MNT_LOCKED by the
# kernel and cannot be unmounted from a child mount namespace.
log_must zfs unmount "$TESTPOOL/$TESTFS/deleg_root/child1"
log_must zfs unmount "$TESTPOOL/$TESTFS/deleg_root/child2"

# Test 1: Destroy snapshot from user namespace (should succeed)
log_note "Test 1: Destroying snapshot from user namespace..."
typeset snap_result
snap_result=$(run_in_userns "$ZONED_TEST_UID" \
    destroy "$TESTPOOL/$TESTFS/deleg_root/child1@snap1" 2>&1)
typeset snap_status=$?

if [[ $snap_status -ne 0 ]]; then
	log_note "Destroy snapshot output: $snap_result"
	log_fail "Failed to destroy snapshot from user namespace"
fi

# Verify snapshot is gone
if zfs list -t snapshot "$TESTPOOL/$TESTFS/deleg_root/child1@snap1" 2>/dev/null; then
	log_fail "Snapshot should have been destroyed"
fi
log_note "Snapshot destroyed successfully"

# Test 2: Destroy child dataset from user namespace (should succeed)
log_note "Test 2: Destroying child dataset from user namespace..."
typeset child_result
child_result=$(run_in_userns "$ZONED_TEST_UID" \
    destroy "$TESTPOOL/$TESTFS/deleg_root/child1" 2>&1)
typeset child_status=$?

if [[ $child_status -ne 0 ]]; then
	log_note "Destroy child output: $child_result"
	log_fail "Failed to destroy child dataset from user namespace"
fi

# Verify child is gone
if zfs list "$TESTPOOL/$TESTFS/deleg_root/child1" 2>/dev/null; then
	log_fail "Child dataset should have been destroyed"
fi
log_note "Child dataset destroyed successfully"

# Test 3: Attempt to destroy delegation root (should FAIL - protected)
log_note "Test 3: Attempting to destroy delegation root (should fail)..."
typeset root_result
root_result=$(run_in_userns "$ZONED_TEST_UID" \
    destroy "$TESTPOOL/$TESTFS/deleg_root" 2>&1)
typeset root_status=$?

if [[ $root_status -eq 0 ]]; then
	log_fail "Destroying delegation root should have been denied"
fi

log_note "Delegation root destruction correctly denied: $root_result"

# Verify delegation root still exists
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root"
log_note "Delegation root still exists (protected)"

# Verify remaining child still exists
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/child2"
log_note "Remaining child dataset unaffected"

log_pass "Authorized user namespace can destroy children but not delegation root"
