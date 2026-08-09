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
#	Verify that a user namespace with a non-matching UID cannot perform
#	write operations on datasets delegated to a different UID.
#
# STRATEGY:
#	1. Create a delegation root with zoned_uid set to ZONED_TEST_UID
#	2. Enter a user namespace owned by ZONED_OTHER_UID (different)
#	3. Verify dataset is visible (read-only path visibility)
#	4. Attempt to create child dataset (should fail)
#	5. Attempt to create snapshot (should fail)
#	6. Attempt to set property (should fail)
#	7. Attempt to destroy (should fail)
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Unauthorized user namespace cannot perform write operations"
log_onexit cleanup

# Create delegation root owned by ZONED_TEST_UID
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/child"

log_note "Created delegation root with zoned_uid=$ZONED_TEST_UID"
log_note "Will test access from user namespace owned by $ZONED_OTHER_UID"

# Test 1: Verify dataset visibility (should be visible via parent path)
# Note: The dataset may or may not be visible depending on implementation
# The key test is that write operations fail
log_note "Test 1: Checking visibility from wrong user namespace..."
typeset list_result
list_result=$(run_in_userns "$ZONED_OTHER_UID" \
    list "$TESTPOOL/$TESTFS/deleg_root" 2>&1)
list_status=$?
log_note "List result (status=$list_status): $list_result"

# Test 2: Attempt to create child dataset (should FAIL)
log_note "Test 2: Attempting to create child from wrong namespace (should fail)..."
typeset create_result
create_result=$(run_in_userns "$ZONED_OTHER_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root/unauthorized_child" 2>&1)
create_status=$?

if [[ $create_status -eq 0 ]]; then
	log_fail "Creating child from unauthorized namespace should have been denied"
fi
log_note "Create correctly denied: $create_result"

# Verify the unauthorized child was not created
if zfs list "$TESTPOOL/$TESTFS/deleg_root/unauthorized_child" 2>/dev/null; then
	log_fail "Unauthorized child dataset should not exist"
fi

# Test 3: Attempt to create snapshot (should FAIL)
log_note "Test 3: Attempting to create snapshot from wrong namespace (should fail)..."
typeset snap_result
snap_result=$(run_in_userns "$ZONED_OTHER_UID" \
    snapshot "$TESTPOOL/$TESTFS/deleg_root/child@unauthorized" 2>&1)
snap_status=$?

if [[ $snap_status -eq 0 ]]; then
	log_fail "Creating snapshot from unauthorized namespace should have been denied"
fi
log_note "Snapshot correctly denied: $snap_result"

# Test 4: Attempt to set property (should FAIL)
log_note "Test 4: Attempting to set property from wrong namespace (should fail)..."
typeset prop_result
prop_result=$(run_in_userns "$ZONED_OTHER_UID" \
    set quota=1G "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
prop_status=$?

if [[ $prop_status -eq 0 ]]; then
	log_fail "Setting property from unauthorized namespace should have been denied"
fi
log_note "Set property correctly denied: $prop_result"

# Verify quota was not changed
typeset actual_quota
actual_quota=$(zfs get -H -o value quota "$TESTPOOL/$TESTFS/deleg_root/child")
if [[ "$actual_quota" == "1G" ]]; then
	log_fail "Quota should not have been changed by unauthorized namespace"
fi

# Test 5: Attempt to destroy (should FAIL)
log_note "Test 5: Attempting to destroy from wrong namespace (should fail)..."
typeset destroy_result
destroy_result=$(run_in_userns "$ZONED_OTHER_UID" \
    destroy "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
destroy_status=$?

if [[ $destroy_status -eq 0 ]]; then
	log_fail "Destroying from unauthorized namespace should have been denied"
fi
log_note "Destroy correctly denied: $destroy_result"

# Verify child still exists
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/child"
log_note "Child dataset still exists (protected from unauthorized access)"

# Test 6: Attempt to rename (should FAIL)
log_note "Test 6: Attempting to rename from wrong namespace (should fail)..."
typeset rename_result
rename_result=$(run_in_userns "$ZONED_OTHER_UID" \
    rename "$TESTPOOL/$TESTFS/deleg_root/child" \
    "$TESTPOOL/$TESTFS/deleg_root/child_renamed" 2>&1)
rename_status=$?

if [[ $rename_status -eq 0 ]]; then
	log_fail "Renaming from unauthorized namespace should have been denied"
fi
log_note "Rename correctly denied: $rename_result"

# Verify child still has original name
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/child"

log_pass "Unauthorized user namespace cannot perform write operations"
