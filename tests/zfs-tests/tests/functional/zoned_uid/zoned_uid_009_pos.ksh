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
#	Verify that an authorized user namespace can rename datasets within
#	the delegation subtree, but cannot rename datasets outside of it.
#
# STRATEGY:
#	1. Create a delegation root with zoned_uid set
#	2. Create child datasets
#	3. Enter user namespace and rename within the subtree (should succeed)
#	4. Attempt to rename outside the subtree (should fail)
#	5. Verify the rename operations behaved correctly
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf $TESTPOOL/$TESTFS/deleg_root 2>/dev/null
	zfs destroy -rf $TESTPOOL/$TESTFS/outside 2>/dev/null
}

log_assert "Authorized user namespace can rename within delegation subtree only"
log_onexit cleanup

# Create delegation root with children
log_must zfs create $TESTPOOL/$TESTFS/deleg_root
log_must set_zoned_uid $TESTPOOL/$TESTFS/deleg_root $ZONED_TEST_UID
log_must zfs create $TESTPOOL/$TESTFS/deleg_root/child1
log_must zfs create $TESTPOOL/$TESTFS/deleg_root/subdir
log_must zfs create $TESTPOOL/$TESTFS/deleg_root/subdir/nested

# Create a dataset outside the delegation root (for escape test)
log_must zfs create $TESTPOOL/$TESTFS/outside

log_note "Created delegation root with children and outside dataset"

# Unmount datasets from global zone before entering user namespace.
# Mounts inherited from the parent mount namespace are MNT_LOCKED by the
# kernel and cannot be unmounted from a child mount namespace.
log_must zfs unmount $TESTPOOL/$TESTFS/deleg_root/subdir/nested
log_must zfs unmount $TESTPOOL/$TESTFS/deleg_root/subdir
log_must zfs unmount $TESTPOOL/$TESTFS/deleg_root/child1
log_must zfs unmount $TESTPOOL/$TESTFS/outside

# Test 1: Rename within subtree (should succeed)
log_note "Test 1: Renaming within delegation subtree..."
typeset rename_result
rename_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs rename $TESTPOOL/$TESTFS/deleg_root/child1 \
    $TESTPOOL/$TESTFS/deleg_root/child1_renamed 2>&1)
typeset rename_status=$?

if [[ $rename_status -ne 0 ]]; then
	log_note "Rename output: $rename_result"
	log_fail "Failed to rename within delegation subtree"
fi

# Verify old name is gone and new name exists
if zfs list $TESTPOOL/$TESTFS/deleg_root/child1 2>/dev/null; then
	log_fail "Old dataset name should not exist after rename"
fi
log_must zfs list $TESTPOOL/$TESTFS/deleg_root/child1_renamed
log_note "Rename within subtree succeeded"

# Test 2: Rename to a different location within subtree (should succeed)
log_note "Test 2: Moving dataset within subtree..."
typeset move_result
move_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs rename $TESTPOOL/$TESTFS/deleg_root/subdir/nested \
    $TESTPOOL/$TESTFS/deleg_root/nested_moved 2>&1)
typeset move_status=$?

if [[ $move_status -ne 0 ]]; then
	log_note "Move output: $move_result"
	log_fail "Failed to move dataset within delegation subtree"
fi

log_must zfs list $TESTPOOL/$TESTFS/deleg_root/nested_moved
log_note "Move within subtree succeeded"

# Test 3: Attempt to rename outside the subtree (should FAIL)
log_note "Test 3: Attempting to rename outside subtree (should fail)..."
typeset escape_result
escape_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs rename $TESTPOOL/$TESTFS/deleg_root/child1_renamed \
    $TESTPOOL/$TESTFS/outside/escaped 2>&1)
typeset escape_status=$?

if [[ $escape_status -eq 0 ]]; then
	log_fail "Renaming outside delegation subtree should have been denied"
fi

log_note "Rename outside subtree correctly denied: $escape_result"

# Verify the dataset is still in its original location
log_must zfs list $TESTPOOL/$TESTFS/deleg_root/child1_renamed
log_note "Dataset remains in delegation subtree"

# Test 4: Attempt to rename from outside into subtree (should FAIL)
# This tests that we can't "steal" datasets from outside
log_note "Test 4: Attempting to rename from outside into subtree (should fail)..."
typeset steal_result
steal_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs rename $TESTPOOL/$TESTFS/outside \
    $TESTPOOL/$TESTFS/deleg_root/stolen 2>&1)
typeset steal_status=$?

if [[ $steal_status -eq 0 ]]; then
	log_fail "Renaming from outside into subtree should have been denied"
fi

log_note "Rename from outside correctly denied: $steal_result"

# Verify outside dataset still exists in original location
log_must zfs list $TESTPOOL/$TESTFS/outside
log_note "Outside dataset remains in place"

log_pass "Authorized user namespace can rename within delegation subtree only"
