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
#	Namespace-initiated rename and destroy do not retain visibility
#	after the administrator revokes zoned_uid delegation.
#
# STRATEGY:
#	1. Create delegation root with zoned_uid, grant permissions
#	2. From namespace: rename child → child2
#	3. From namespace: destroy child2
#	4. Admin removes zoned_uid delegation (zfs set zoned_uid=0)
#	5. Verify delegation root is not visible from the old namespace
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Zone tracking cleanup after namespace rename+destroy"
log_onexit cleanup

# Setup: delegation root with child, mountpoint=none to avoid mount-lock issues
log_must zfs create -o mountpoint=none "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/child"

# Grant all needed permissions
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    "create,destroy,rename,mount"

log_note "Test: rename child from namespace"
typeset result
result=$(run_in_userns "$ZONED_TEST_UID" \
    rename "$TESTPOOL/$TESTFS/deleg_root/child" \
    "$TESTPOOL/$TESTFS/deleg_root/child2" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "rename should succeed from namespace"
fi
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/child2"
log_note "Rename succeeded"

log_note "Test: destroy renamed child from namespace"
result=$(run_in_userns "$ZONED_TEST_UID" \
    destroy "$TESTPOOL/$TESTFS/deleg_root/child2" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "destroy should succeed from namespace"
fi
log_note "Destroy succeeded"

log_note "Test: admin removes zoned_uid delegation"
log_must zfs set zoned_uid=0 "$TESTPOOL/$TESTFS/deleg_root"

log_note "Test: verify no stale visibility after delegation removal"
result=$(run_in_userns "$ZONED_TEST_UID" \
    list "$TESTPOOL/$TESTFS/deleg_root" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "Delegation root should NOT be visible after " \
	    "zoned_uid=0 (stale tracking entry detected)"
fi
log_note "No stale visibility: delegation root correctly hidden"

log_pass "Zone tracking cleanup after namespace rename+destroy"
