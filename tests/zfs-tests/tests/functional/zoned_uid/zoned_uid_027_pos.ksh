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
#	CAP_SYS_ADMIN satisfies the L2 requirement for ALL operation tiers
#	(both non-destructive and destructive).  This verifies that
#	SYS_ADMIN is a superset of FOWNER for L2 purposes.
#
# STRATEGY:
#	1. Create delegation root with zoned_uid
#	2. Grant all permissions via zfs allow
#	3. With CAP_SYS_ADMIN: create succeeds (SYS_ADMIN covers FOWNER tier)
#	4. With CAP_SYS_ADMIN: snapshot succeeds
#	5. With CAP_SYS_ADMIN: destroy succeeds
#	6. Verify CAP_SYS_ADMIN is a complete L2 pass for all ops
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "CAP_SYS_ADMIN satisfies L2 for all operation tiers"
log_onexit cleanup

# Setup
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    "create,destroy,snapshot,rename,clone,mount"

# ADD-7: create with dsl_deleg + CAP_SYS_ADMIN → allowed
log_note "Test ADD-7: create with CAP_SYS_ADMIN"
typeset result
result=$(run_in_userns "$ZONED_TEST_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root/child1" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "ADD-7: create should succeed with SYS_ADMIN"
fi
log_note "ADD-7 passed: create allowed with SYS_ADMIN"

# Snapshot with CAP_SYS_ADMIN
log_note "Test: snapshot with CAP_SYS_ADMIN"
result=$(run_in_userns "$ZONED_TEST_UID" \
    snapshot "$TESTPOOL/$TESTFS/deleg_root/child1@snap1" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "snapshot should succeed with SYS_ADMIN"
fi
log_note "Snapshot passed with SYS_ADMIN"

# Clone with CAP_SYS_ADMIN (destructive tier)
log_note "Test: clone with CAP_SYS_ADMIN"
result=$(run_in_userns "$ZONED_TEST_UID" \
    clone "$TESTPOOL/$TESTFS/deleg_root/child1@snap1" \
    "$TESTPOOL/$TESTFS/deleg_root/clone1" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "clone should succeed with SYS_ADMIN"
fi
log_note "Clone passed with SYS_ADMIN"

# Destroy with CAP_SYS_ADMIN (destructive tier)
log_note "Test: destroy with CAP_SYS_ADMIN"
result=$(run_in_userns "$ZONED_TEST_UID" \
    destroy "$TESTPOOL/$TESTFS/deleg_root/clone1" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "destroy should succeed with SYS_ADMIN"
fi
log_note "Destroy passed with SYS_ADMIN"

log_pass "CAP_SYS_ADMIN satisfies L2 for all operation tiers"
