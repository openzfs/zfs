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
#	Additive least privilege: non-destructive operations (create, snapshot,
#	setprop) succeed only when BOTH dsl_deleg grants the permission AND
#	the namespace has at least CAP_FOWNER.
#
# STRATEGY:
#	1. Create delegation root with zoned_uid
#	2. Grant create,snapshot,mount via zfs allow
#	3. With CAP_FOWNER: create succeeds (L1 yes + L2 yes)
#	4. With no caps: create fails (L1 yes, L2 no)
#	5. Without zfs allow grant: create fails even with CAP_FOWNER (L1 no)
#	6. With CAP_FOWNER + snapshot grant: snapshot succeeds
#	7. With CAP_FOWNER + create grant only: snapshot fails (wrong perm)
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Additive L1+L2: non-destructive ops need dsl_deleg AND CAP_FOWNER"
log_onexit cleanup

# Step 1: Create delegation root.
# Use mountpoint=none so create/snapshot from the namespace don't
# trigger mount operations that would fail without CAP_SYS_ADMIN.
log_must zfs create -o mountpoint=none "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"

# Step 2: Grant create,snapshot,mount
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    "create,snapshot,mount"

# ADD-1: L1 grants create + L2 has CAP_FOWNER → allowed
log_note "Test ADD-1: create with dsl_deleg + CAP_FOWNER"
typeset result
result=$(run_in_userns_caps "$ZONED_TEST_UID" "drop_sys_admin" \
    create "$TESTPOOL/$TESTFS/deleg_root/add1_child" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "ADD-1: create should succeed with dsl_deleg + CAP_FOWNER"
fi
log_note "ADD-1 passed: create allowed"

# ADD-2: L1 grants create + L2 has no caps → denied
log_note "Test ADD-2: create with dsl_deleg + no caps"
result=$(run_in_userns_caps "$ZONED_TEST_UID" "none" \
    create "$TESTPOOL/$TESTFS/deleg_root/add2_child" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "ADD-2: create should fail without capabilities"
fi
log_note "ADD-2 passed: create denied without caps"

# Verify the dataset was NOT created
if datasetexists "$TESTPOOL/$TESTFS/deleg_root/add2_child"; then
	log_fail "ADD-2: dataset should not exist"
fi

# ADD-3: No dsl_deleg grant + CAP_FOWNER → denied
log_note "Test ADD-3: create without dsl_deleg grant"
log_must revoke_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
result=$(run_in_userns_caps "$ZONED_TEST_UID" "drop_sys_admin" \
    create "$TESTPOOL/$TESTFS/deleg_root/add3_child" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "ADD-3: create should fail without dsl_deleg grant"
fi
log_note "ADD-3 passed: create denied without dsl_deleg"

if datasetexists "$TESTPOOL/$TESTFS/deleg_root/add3_child"; then
	log_fail "ADD-3: dataset should not exist"
fi

# ADD-5: Restore grants, test snapshot with CAP_FOWNER
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    "create,snapshot,mount"

log_note "Test ADD-5: snapshot with dsl_deleg + CAP_FOWNER"
result=$(run_in_userns_caps "$ZONED_TEST_UID" "drop_sys_admin" \
    snapshot "$TESTPOOL/$TESTFS/deleg_root/add1_child@snap1" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "ADD-5: snapshot should succeed with dsl_deleg + CAP_FOWNER"
fi
log_note "ADD-5 passed: snapshot allowed"

# ADD-6: create grant only, snapshot should fail (wrong perm)
log_must revoke_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    "create,mount"

log_note "Test ADD-6: snapshot with create-only dsl_deleg"
result=$(run_in_userns_caps "$ZONED_TEST_UID" "drop_sys_admin" \
    snapshot "$TESTPOOL/$TESTFS/deleg_root/add1_child@snap_bad" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "ADD-6: snapshot should fail with create-only dsl_deleg"
fi
log_note "ADD-6 passed: snapshot denied (wrong perm in dsl_deleg)"

log_pass "Additive L1+L2: non-destructive ops need dsl_deleg AND CAP_FOWNER"
