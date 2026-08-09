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
#	Read-only operations (list, get properties) require no capabilities
#	and no dsl_deleg grants.  Visibility is controlled solely by the
#	zoned_uid delegation scoping.
#
# STRATEGY:
#	1. Create delegation root with zoned_uid and a child
#	2. No dsl_deleg grants, no capabilities
#	3. From namespace: zfs list succeeds for delegated dataset
#	4. From namespace: zfs get succeeds for delegated dataset
#	5. From namespace: zfs list fails for non-delegated dataset
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Read-only operations need no caps and no dsl_deleg"
log_onexit cleanup

# Setup: delegation root with a child, NO zfs allow grants.
# Use mountpoint=none to avoid mount-lock issues in user namespaces.
log_must zfs create -o mountpoint=none "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/child"

# ADD-14: list with no caps, no dsl_deleg → allowed (read-only)
log_note "Test ADD-14: list delegated dataset with no caps"
typeset result
result=$(run_in_userns_caps "$ZONED_TEST_UID" "none" \
    list "$TESTPOOL/$TESTFS/deleg_root" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "ADD-14: list should succeed with no caps (read-only)"
fi
log_note "ADD-14 passed: list allowed"

# Get properties with no caps → should work
log_note "Test: get properties with no caps"
result=$(run_in_userns_caps "$ZONED_TEST_UID" "none" \
    get zoned_uid "$TESTPOOL/$TESTFS/deleg_root" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "get properties should succeed with no caps (read-only)"
fi
log_note "Get properties passed"

# List child dataset with no caps → should work (child of delegation)
log_note "Test: list child dataset with no caps"
result=$(run_in_userns_caps "$ZONED_TEST_UID" "none" \
    list "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "list child should succeed with no caps"
fi
log_note "List child passed"

# Non-delegated dataset should NOT be visible
log_note "Test: list non-delegated dataset from namespace"
log_must zfs create "$TESTPOOL/$TESTFS/other_ds"
result=$(run_in_userns_caps "$ZONED_TEST_UID" "none" \
    list "$TESTPOOL/$TESTFS/other_ds" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "Non-delegated dataset should not be visible"
fi
log_note "Non-delegated dataset correctly not visible"
log_must zfs destroy "$TESTPOOL/$TESTFS/other_ds"

log_pass "Read-only operations need no caps and no dsl_deleg"
