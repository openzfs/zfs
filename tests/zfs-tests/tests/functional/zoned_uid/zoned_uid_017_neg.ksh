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
#	Verify that a namespace user cannot modify the zoned_uid property,
#	even on datasets they have delegation over. Only the global zone
#	admin should be able to manage delegation assignments.
#
# STRATEGY:
#	1. Create delegation root with zoned_uid=$ZONED_TEST_UID
#	2. Create child dataset (inherits zoned_uid)
#	3. From namespace: attempt zfs set zoned_uid=none on child (should FAIL)
#	4. Verify zoned_uid still inherited on child
#	5. From namespace: attempt zfs set zoned_uid=$ZONED_OTHER_UID (should FAIL)
#	6. From namespace: attempt zfs set zoned_uid=$ZONED_TEST_UID (should FAIL)
#	7. From namespace: attempt zfs set zoned_uid=none on root (should FAIL)
#	8. Verify delegation root still has original zoned_uid
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Namespace user cannot modify zoned_uid property"
log_onexit cleanup

# Step 1-2: Create delegation root and child
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/child"

log_note "Created delegation root and child with zoned_uid=$ZONED_TEST_UID"

# Step 3: Attempt to clear zoned_uid on child from namespace (should FAIL)
log_note "Test 1: Attempting zfs set zoned_uid=none on child from namespace..."
typeset result
result=$(run_in_userns "$ZONED_TEST_UID" \
    set zoned_uid=none "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
typeset status=$?

if [[ $status -eq 0 ]]; then
	log_fail "Setting zoned_uid=none on child should have been denied"
fi
log_note "Correctly denied: $result"

# Step 4: Verify zoned_uid still inherited on child
typeset child_uid
child_uid=$(get_zoned_uid "$TESTPOOL/$TESTFS/deleg_root/child")
if [[ "$child_uid" != "$ZONED_TEST_UID" ]]; then
	log_fail "Child zoned_uid changed to '$child_uid', expected '$ZONED_TEST_UID'"
fi
log_note "Child zoned_uid still $ZONED_TEST_UID (inherited)"

# Step 5: Attempt to change zoned_uid to different UID (should FAIL)
log_note "Test 2: Attempting zfs set zoned_uid=$ZONED_OTHER_UID on child..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    set "zoned_uid=$ZONED_OTHER_UID" "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
status=$?

if [[ $status -eq 0 ]]; then
	log_fail "Setting zoned_uid to different UID should have been denied"
fi
log_note "Correctly denied: $result"

# Step 6: Attempt to set zoned_uid to same UID (should still FAIL)
log_note "Test 3: Attempting zfs set zoned_uid=$ZONED_TEST_UID on child..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    set "zoned_uid=$ZONED_TEST_UID" "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
status=$?

if [[ $status -eq 0 ]]; then
	log_fail "Setting zoned_uid (even to same value) should have been denied"
fi
log_note "Correctly denied: $result"

# Step 7: Attempt to clear zoned_uid on delegation root (should FAIL)
log_note "Test 4: Attempting zfs set zoned_uid=none on delegation root..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    set zoned_uid=none "$TESTPOOL/$TESTFS/deleg_root" 2>&1)
status=$?

if [[ $status -eq 0 ]]; then
	log_fail "Setting zoned_uid=none on delegation root should have been denied"
fi
log_note "Correctly denied: $result"

# Step 8: Verify delegation root still has original zoned_uid
typeset root_uid
root_uid=$(get_zoned_uid "$TESTPOOL/$TESTFS/deleg_root")
if [[ "$root_uid" != "$ZONED_TEST_UID" ]]; then
	log_fail "Root zoned_uid changed to '$root_uid', expected '$ZONED_TEST_UID'"
fi
log_note "Delegation root zoned_uid still $ZONED_TEST_UID"

log_pass "Namespace user cannot modify zoned_uid property"
