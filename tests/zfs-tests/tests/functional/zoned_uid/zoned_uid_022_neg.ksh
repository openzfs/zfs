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
#	Verify that delegated users cannot override filesystem_limit and
#	snapshot_limit set by the global admin on the delegation root.
#	Delegated users CAN set tighter sub-limits on child datasets.
#
# STRATEGY:
#	1. Create delegation root with zoned_uid
#	2. Global admin: set filesystem_limit=10, snapshot_limit=5
#	3. From namespace: attempt filesystem_limit=none on root (FAIL)
#	4. From namespace: attempt snapshot_limit=none on root (FAIL)
#	5. Verify limits unchanged on delegation root
#	6. From namespace: create child dataset
#	7. From namespace: set filesystem_limit=3 on child (SUCCEED)
#	8. From namespace: set snapshot_limit=2 on child (SUCCEED)
#	9. Verify child has the sub-limits set
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Delegated user cannot override admin limits on delegation root"
log_onexit cleanup

# Step 1: Create delegation root
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    create,mount,filesystem_limit,snapshot_limit

# Step 2: Global admin sets limits
log_must zfs set filesystem_limit=10 "$TESTPOOL/$TESTFS/deleg_root"
log_must zfs set snapshot_limit=5 "$TESTPOOL/$TESTFS/deleg_root"

log_note "Admin set filesystem_limit=10, snapshot_limit=5 on delegation root"

# Step 3: Attempt to remove filesystem_limit from namespace (should FAIL)
log_note "Test 1: filesystem_limit=none on root from namespace..."
typeset result
result=$(run_in_userns "$ZONED_TEST_UID" \
    set filesystem_limit=none "$TESTPOOL/$TESTFS/deleg_root" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "Removing filesystem_limit on root should have been denied"
fi
log_note "Correctly denied: $result"

# Also try raising the limit
log_note "Test 2: filesystem_limit=100 on root from namespace..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    set filesystem_limit=100 "$TESTPOOL/$TESTFS/deleg_root" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "Raising filesystem_limit on root should have been denied"
fi
log_note "Correctly denied: $result"

# Step 4: Attempt to remove snapshot_limit from namespace (should FAIL)
log_note "Test 3: snapshot_limit=none on root from namespace..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    set snapshot_limit=none "$TESTPOOL/$TESTFS/deleg_root" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "Removing snapshot_limit on root should have been denied"
fi
log_note "Correctly denied: $result"

# Step 5: Verify limits unchanged
typeset fs_limit snap_limit
fs_limit=$(get_prop filesystem_limit "$TESTPOOL/$TESTFS/deleg_root")
snap_limit=$(get_prop snapshot_limit "$TESTPOOL/$TESTFS/deleg_root")

if [[ "$fs_limit" != "10" ]]; then
	log_fail "filesystem_limit changed to '$fs_limit', expected '10'"
fi
if [[ "$snap_limit" != "5" ]]; then
	log_fail "snapshot_limit changed to '$snap_limit', expected '5'"
fi
log_note "Admin limits unchanged on delegation root"

# Step 6: Create child from namespace
result=$(run_in_userns "$ZONED_TEST_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Create child output: $result"
	log_fail "Failed to create child from namespace"
fi

# Step 7: Set filesystem_limit on child (should SUCCEED - tighter sub-limit)
log_note "Test 4: filesystem_limit=3 on child from namespace..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    set filesystem_limit=3 "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
typeset status=$?
if [[ $status -ne 0 ]]; then
	log_note "Set filesystem_limit on child output: $result"
	log_fail "Setting filesystem_limit on child should succeed (status=$status)"
fi

# Step 8: Set snapshot_limit on child (should SUCCEED)
log_note "Test 5: snapshot_limit=2 on child from namespace..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    set snapshot_limit=2 "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
status=$?
if [[ $status -ne 0 ]]; then
	log_note "Set snapshot_limit on child output: $result"
	log_fail "Setting snapshot_limit on child should succeed (status=$status)"
fi

# Step 9: Verify child has the sub-limits
typeset child_fs_limit child_snap_limit
child_fs_limit=$(get_prop filesystem_limit \
    "$TESTPOOL/$TESTFS/deleg_root/child")
child_snap_limit=$(get_prop snapshot_limit \
    "$TESTPOOL/$TESTFS/deleg_root/child")

if [[ "$child_fs_limit" != "3" ]]; then
	log_fail "Child filesystem_limit should be 3, got: $child_fs_limit"
fi
if [[ "$child_snap_limit" != "2" ]]; then
	log_fail "Child snapshot_limit should be 2, got: $child_snap_limit"
fi
log_note "Child has correct sub-limits: filesystem_limit=3, snapshot_limit=2"

log_pass "Delegated user cannot override admin limits on delegation root"
