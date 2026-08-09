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
#	Cross-cutting constraints still apply under the additive model.
#	Even with full dsl_deleg grants AND CAP_SYS_ADMIN, certain
#	operations are always denied to protect the delegation boundary.
#
# STRATEGY:
#	1. Create delegation root with full grants + CAP_SYS_ADMIN
#	2. CROSS-1: Cannot destroy delegation root itself
#	3. CROSS-2: Cannot rename dataset outside delegation subtree
#	4. CROSS-3: Cannot modify zoned_uid property from namespace
#	5. CROSS-4: Cannot override admin-set limits on delegation root
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
	zfs destroy -rf "$TESTPOOL/$TESTFS/outside" 2>/dev/null
}

log_assert "Cross-cutting constraints enforced under additive model"
log_onexit cleanup

# Setup: full permissions
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/child"
log_must zfs create "$TESTPOOL/$TESTFS/outside"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    "create,destroy,snapshot,rename,clone,mount"
log_must zfs set filesystem_limit=10 "$TESTPOOL/$TESTFS/deleg_root"
log_must zfs set snapshot_limit=5 "$TESTPOOL/$TESTFS/deleg_root"

# CROSS-1: Cannot destroy the delegation root itself
log_note "Test CROSS-1: destroy delegation root"
run_in_userns "$ZONED_TEST_UID" \
    destroy "$TESTPOOL/$TESTFS/deleg_root" >/dev/null 2>&1
if [[ $? -eq 0 ]]; then
	log_fail "CROSS-1: should not be able to destroy delegation root"
fi
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root"
log_note "CROSS-1 passed: delegation root protected"

# CROSS-2: Cannot rename outside delegation subtree
log_note "Test CROSS-2: rename outside subtree"
run_in_userns "$ZONED_TEST_UID" \
    rename "$TESTPOOL/$TESTFS/deleg_root/child" \
    "$TESTPOOL/$TESTFS/outside/escaped" >/dev/null 2>&1
if [[ $? -eq 0 ]]; then
	log_fail "CROSS-2: should not be able to rename outside subtree"
fi
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/child"
log_note "CROSS-2 passed: cannot escape delegation"

# CROSS-3: Cannot modify zoned_uid from namespace
log_note "Test CROSS-3: set zoned_uid from namespace"
run_in_userns "$ZONED_TEST_UID" \
    set zoned_uid=0 "$TESTPOOL/$TESTFS/deleg_root" >/dev/null 2>&1
if [[ $? -eq 0 ]]; then
	log_fail "CROSS-3: should not be able to modify zoned_uid"
fi
typeset val
val=$(get_zoned_uid "$TESTPOOL/$TESTFS/deleg_root")
if [[ "$val" != "$ZONED_TEST_UID" ]]; then
	log_fail "CROSS-3: zoned_uid changed from $ZONED_TEST_UID to $val"
fi
log_note "CROSS-3 passed: zoned_uid protected"

# CROSS-4: Cannot override admin limits on delegation root
log_note "Test CROSS-4: override filesystem_limit on root"
run_in_userns "$ZONED_TEST_UID" \
    set filesystem_limit=none "$TESTPOOL/$TESTFS/deleg_root" >/dev/null 2>&1
if [[ $? -eq 0 ]]; then
	log_fail "CROSS-4: should not be able to remove admin limits"
fi
typeset fs_limit
fs_limit=$(get_prop filesystem_limit "$TESTPOOL/$TESTFS/deleg_root")
if [[ "$fs_limit" != "10" ]]; then
	log_fail "CROSS-4: filesystem_limit changed to $fs_limit"
fi

run_in_userns "$ZONED_TEST_UID" \
    set snapshot_limit=none "$TESTPOOL/$TESTFS/deleg_root" >/dev/null 2>&1
if [[ $? -eq 0 ]]; then
	log_fail "CROSS-4: should not be able to remove snapshot_limit"
fi
log_note "CROSS-4 passed: admin limits protected"

log_pass "Cross-cutting constraints enforced under additive model"
