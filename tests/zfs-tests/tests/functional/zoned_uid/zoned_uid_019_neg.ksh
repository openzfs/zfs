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
#	Verify that two different UIDs with sibling delegations cannot
#	access each other's subtrees (multi-UID isolation).
#
# STRATEGY:
#	1. Create two sibling delegation roots with different zoned_uids
#	2. Create a child under each from global zone
#	3. From UID A's namespace: verify can create under deleg_root_a
#	4. From UID A's namespace: attempt create under deleg_root_b (FAIL)
#	5. From UID A's namespace: attempt destroy child under deleg_root_b (FAIL)
#	6. From UID A's namespace: attempt set property on deleg_root_b/child (FAIL)
#	7. From UID B's namespace: verify can create under deleg_root_b
#	8. From UID B's namespace: attempt create under deleg_root_a (FAIL)
#	9. Verify both subtrees remain intact
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root_a" 2>/dev/null
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root_b" 2>/dev/null
}

log_assert "Multi-UID isolation: sibling delegations cannot cross boundaries"
log_onexit cleanup

# Step 1: Create two sibling delegation roots
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root_a"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root_a" "$ZONED_TEST_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root_a" "$ZONED_TEST_UID" \
    create,mount

log_must zfs create "$TESTPOOL/$TESTFS/deleg_root_b"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root_b" "$ZONED_OTHER_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root_b" "$ZONED_OTHER_UID" \
    create,mount

# Step 2: Create a child under each from global zone
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root_a/child_a"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root_b/child_b"

log_note "Created two delegation roots: A(uid=$ZONED_TEST_UID) B(uid=$ZONED_OTHER_UID)"

# Step 3: UID A can create under its own subtree
typeset result
result=$(run_in_userns "$ZONED_TEST_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root_a/ns_child_a" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Create output: $result"
	log_fail "UID A should be able to create under deleg_root_a"
fi
log_note "UID A can create under its own subtree"

# Step 4: UID A cannot create under UID B's subtree
result=$(run_in_userns "$ZONED_TEST_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root_b/intruder_a" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "UID A should NOT be able to create under deleg_root_b"
fi
log_note "UID A correctly denied create under deleg_root_b"

# Step 5: UID A cannot destroy child under UID B's subtree
result=$(run_in_userns "$ZONED_TEST_UID" \
    destroy "$TESTPOOL/$TESTFS/deleg_root_b/child_b" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "UID A should NOT be able to destroy under deleg_root_b"
fi
log_note "UID A correctly denied destroy under deleg_root_b"

# Step 6: UID A cannot set property on UID B's subtree
result=$(run_in_userns "$ZONED_TEST_UID" \
    set mountpoint=none "$TESTPOOL/$TESTFS/deleg_root_b/child_b" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "UID A should NOT be able to set properties on deleg_root_b"
fi
log_note "UID A correctly denied setprop on deleg_root_b"

# Step 7: UID B can create under its own subtree
result=$(run_in_userns "$ZONED_OTHER_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root_b/ns_child_b" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Create output: $result"
	log_fail "UID B should be able to create under deleg_root_b"
fi
log_note "UID B can create under its own subtree"

# Step 8: UID B cannot create under UID A's subtree
result=$(run_in_userns "$ZONED_OTHER_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root_a/intruder_b" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "UID B should NOT be able to create under deleg_root_a"
fi
log_note "UID B correctly denied create under deleg_root_a"

# Step 9: Verify both subtrees remain intact
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root_a/child_a"
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root_a/ns_child_a"
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root_b/child_b"
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root_b/ns_child_b"

# Verify intruder datasets don't exist
if datasetexists "$TESTPOOL/$TESTFS/deleg_root_b/intruder_a"; then
	log_fail "Intruder dataset from UID A should not exist"
fi
if datasetexists "$TESTPOOL/$TESTFS/deleg_root_a/intruder_b"; then
	log_fail "Intruder dataset from UID B should not exist"
fi
log_note "Both subtrees intact, no cross-contamination"

log_pass "Multi-UID isolation: sibling delegations cannot cross boundaries"
