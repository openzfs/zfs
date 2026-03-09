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
#	Verify that operations without zone_dataset_admin_check() integration
#	are denied from a delegated namespace. These operations go through
#	zfs_dozonecheck_impl() which requires zoned=on (not set in the
#	zoned_uid-only flow), so they should all fail with EPERM.
#
# STRATEGY:
#	1. Create delegation root with zoned_uid, create child, create snapshot
#	2. From namespace: attempt zfs send (should FAIL)
#	3. From namespace: attempt zfs rollback (should FAIL)
#	4. From namespace: attempt zfs hold (should FAIL)
#	5. From namespace: attempt zfs bookmark (should FAIL)
#	6. From namespace: attempt zfs allow (should FAIL)
#	7. From namespace: attempt zfs promote on a clone (should FAIL)
#	8. Verify dataset state unchanged
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Operations without admin_check integration are denied from namespace"
log_onexit cleanup

# Step 1: Setup — create delegation root, child, snapshot, and clone
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    create,mount,snapshot

typeset result
result=$(run_in_userns "$ZONED_TEST_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Create child output: $result"
	log_fail "Failed to create child from namespace"
fi

result=$(run_in_userns "$ZONED_TEST_UID" \
    snapshot "$TESTPOOL/$TESTFS/deleg_root/child@snap1" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Create snapshot output: $result"
	log_fail "Failed to create snapshot from namespace"
fi

# Create a clone from global zone for promote test
log_must zfs clone "$TESTPOOL/$TESTFS/deleg_root/child@snap1" \
    "$TESTPOOL/$TESTFS/deleg_root/myclone"

log_note "Setup complete: child, child@snap1, myclone"

# Step 2: Attempt zfs send (should FAIL)
log_note "Test 1: zfs send from namespace..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    send "$TESTPOOL/$TESTFS/deleg_root/child@snap1" 2>&1)
typeset status=$?

if [[ $status -eq 0 ]]; then
	log_fail "zfs send should have been denied from namespace"
fi
log_note "Correctly denied: zfs send (status=$status)"

# Step 3: Attempt zfs rollback (should FAIL)
log_note "Test 2: zfs rollback from namespace..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    rollback "$TESTPOOL/$TESTFS/deleg_root/child@snap1" 2>&1)
status=$?

if [[ $status -eq 0 ]]; then
	log_fail "zfs rollback should have been denied from namespace"
fi
log_note "Correctly denied: zfs rollback (status=$status)"

# Step 4: Attempt zfs hold (should FAIL)
log_note "Test 3: zfs hold from namespace..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    hold mytag "$TESTPOOL/$TESTFS/deleg_root/child@snap1" 2>&1)
status=$?

if [[ $status -eq 0 ]]; then
	log_fail "zfs hold should have been denied from namespace"
fi
log_note "Correctly denied: zfs hold (status=$status)"

# Step 5: Attempt zfs bookmark (should FAIL)
log_note "Test 4: zfs bookmark from namespace..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    bookmark "$TESTPOOL/$TESTFS/deleg_root/child@snap1" \
    "$TESTPOOL/$TESTFS/deleg_root/child#bmark1" 2>&1)
status=$?

if [[ $status -eq 0 ]]; then
	log_fail "zfs bookmark should have been denied from namespace"
fi
log_note "Correctly denied: zfs bookmark (status=$status)"

# Step 6: Attempt zfs allow (should FAIL)
log_note "Test 5: zfs allow from namespace..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    allow -e create "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
status=$?

if [[ $status -eq 0 ]]; then
	log_fail "zfs allow should have been denied from namespace"
fi
log_note "Correctly denied: zfs allow (status=$status)"

# Step 7: Attempt zfs promote (should FAIL)
log_note "Test 6: zfs promote from namespace..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    promote "$TESTPOOL/$TESTFS/deleg_root/myclone" 2>&1)
status=$?

if [[ $status -eq 0 ]]; then
	log_fail "zfs promote should have been denied from namespace"
fi
log_note "Correctly denied: zfs promote (status=$status)"

# Step 8: Verify dataset state unchanged
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/child"
log_must zfs list -t snapshot "$TESTPOOL/$TESTFS/deleg_root/child@snap1"
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/myclone"

# Verify no holds were placed
typeset holds
holds=$(zfs holds "$TESTPOOL/$TESTFS/deleg_root/child@snap1" 2>&1 | wc -l)
if [[ $holds -gt 1 ]]; then
	log_fail "Unexpected holds found on snapshot"
fi

# Verify no bookmarks were created
if zfs list -t bookmark "$TESTPOOL/$TESTFS/deleg_root/child#bmark1" 2>/dev/null; then
	log_fail "Bookmark should not exist"
fi

log_note "All datasets unchanged after denied operations"

log_pass "Operations without admin_check integration are denied from namespace"
