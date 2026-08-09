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
#	When pool delegation is disabled (zpool set delegation=off),
#	ALL zoned_uid write operations are denied regardless of
#	capabilities.  Delegation OFF means the pool admin has opted
#	out of delegating access entirely (POLP).
#	Read-only operations (list, get) still succeed.
#
# STRATEGY:
#	1. Create delegation root with zoned_uid
#	2. Disable delegation on the pool
#	3. DOFF-1: create with CAP_FOWNER → denied (delegation off)
#	4. DOFF-2: destroy with CAP_SYS_ADMIN → denied (delegation off)
#	5. DOFF-3: create with all caps → denied (delegation off)
#	6. DOFF-4: list with no caps → allowed (read-only)
#	7. Re-enable delegation
#

verify_runnable "global"

function cleanup
{
	# Always re-enable delegation
	log_must zpool set delegation=on "$TESTPOOL"
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Delegation OFF: all zoned_uid writes denied"
log_onexit cleanup

# Setup.
# Use mountpoint=none to avoid mount-lock issues in user namespaces.
log_must zfs create -o mountpoint=none "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/victim"

# Disable delegation on pool
log_must zpool set delegation=off "$TESTPOOL"
log_note "Pool delegation disabled"

# DOFF-1: create with CAP_FOWNER → denied (delegation off overrides caps)
log_note "Test DOFF-1: create with CAP_FOWNER (delegation off)"
typeset result
result=$(run_in_userns_caps "$ZONED_TEST_UID" "drop_sys_admin" \
    create "$TESTPOOL/$TESTFS/deleg_root/doff1_child" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "DOFF-1: create should fail when delegation is off"
fi
log_note "DOFF-1 passed: create denied (delegation off)"

# DOFF-2: destroy with CAP_SYS_ADMIN → denied (delegation off)
log_note "Test DOFF-2: destroy with CAP_SYS_ADMIN (delegation off)"
result=$(run_in_userns_caps "$ZONED_TEST_UID" "all" \
    destroy "$TESTPOOL/$TESTFS/deleg_root/victim" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "DOFF-2: destroy should fail when delegation is off"
fi
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/victim"
log_note "DOFF-2 passed: destroy denied (delegation off)"

# DOFF-3: create with all caps → denied (delegation off)
log_note "Test DOFF-3: create with all caps (delegation off)"
result=$(run_in_userns_caps "$ZONED_TEST_UID" "all" \
    create "$TESTPOOL/$TESTFS/deleg_root/doff3_child" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "DOFF-3: create should fail when delegation is off"
fi
log_note "DOFF-3 passed: create denied (delegation off)"

# DOFF-4: list with no caps → allowed (read-only)
log_note "Test DOFF-4: list with no caps (delegation off)"
result=$(run_in_userns_caps "$ZONED_TEST_UID" "none" \
    list "$TESTPOOL/$TESTFS/deleg_root" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "DOFF-4: list should succeed with no caps (read-only)"
fi
log_note "DOFF-4 passed"

# Re-enable delegation
log_must zpool set delegation=on "$TESTPOOL"

log_pass "Delegation OFF: all zoned_uid writes denied"
