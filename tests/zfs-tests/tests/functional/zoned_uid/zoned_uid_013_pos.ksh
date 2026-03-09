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
. $STF_SUITE/include/math.shlib

#
# DESCRIPTION:
#	Verify that an authorized user namespace can set userquota
#	and groupquota properties on delegated datasets.
#
# STRATEGY:
#	1. Create a delegation root with zoned_uid set
#	2. Create a child dataset
#	3. Enter user namespace and set userquota on child
#	4. Set groupquota on child
#	5. Verify quotas were applied correctly
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Authorized user namespace can set userquota/groupquota on delegated datasets"
log_onexit cleanup

# Create delegation root with child
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    userquota,groupquota
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/child"

log_note "Created delegation root with child dataset"

# Test 1: Set userquota from user namespace
log_note "Test 1: Setting userquota from user namespace..."
typeset uq_result
uq_result=$(run_in_userns "$ZONED_TEST_UID" \
    set userquota@0=50M "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
typeset uq_status=$?

if [[ $uq_status -ne 0 ]]; then
	log_note "Set userquota output: $uq_result"
	log_fail "Failed to set userquota from user namespace"
fi

# Verify userquota was set (use -p for parseable/raw bytes)
typeset actual_uq
actual_uq=$(zfs get -Hp -o value userquota@0 "$TESTPOOL/$TESTFS/deleg_root/child")
if ! within_percent "$actual_uq" $((50 * 1048576)) 99; then
	log_fail "Userquota not set correctly: expected ~50M, got $actual_uq"
fi
log_note "Userquota set successfully ($actual_uq bytes)"

# Test 2: Set groupquota from user namespace
log_note "Test 2: Setting groupquota from user namespace..."
typeset gq_result
gq_result=$(run_in_userns "$ZONED_TEST_UID" \
    set groupquota@0=100M "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
typeset gq_status=$?

if [[ $gq_status -ne 0 ]]; then
	log_note "Set groupquota output: $gq_result"
	log_fail "Failed to set groupquota from user namespace"
fi

# Verify groupquota was set (use -p for parseable/raw bytes)
typeset actual_gq
actual_gq=$(zfs get -Hp -o value groupquota@0 "$TESTPOOL/$TESTFS/deleg_root/child")
if ! within_percent "$actual_gq" $((100 * 1048576)) 99; then
	log_fail "Groupquota not set correctly: expected ~100M, got $actual_gq"
fi
log_note "Groupquota set successfully ($actual_gq bytes)"

# Test 3: Set userquota on delegation root itself
log_note "Test 3: Setting userquota on delegation root..."
typeset root_uq_result
root_uq_result=$(run_in_userns "$ZONED_TEST_UID" \
    set userquota@0=200M "$TESTPOOL/$TESTFS/deleg_root" 2>&1)
typeset root_uq_status=$?

if [[ $root_uq_status -ne 0 ]]; then
	log_note "Set userquota on root output: $root_uq_result"
	log_fail "Failed to set userquota on delegation root"
fi

typeset actual_root_uq
actual_root_uq=$(zfs get -Hp -o value userquota@0 "$TESTPOOL/$TESTFS/deleg_root")
if ! within_percent "$actual_root_uq" $((200 * 1048576)) 99; then
	log_fail "Root userquota not set correctly: expected ~200M, got $actual_root_uq"
fi
log_note "Delegation root userquota set successfully ($actual_root_uq bytes)"

log_pass "Authorized user namespace can set userquota/groupquota on delegated datasets"
