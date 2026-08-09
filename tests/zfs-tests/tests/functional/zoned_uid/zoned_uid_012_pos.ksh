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
#	Verify that an authorized user namespace can inherit properties
#	on datasets within the delegation subtree.
#
# STRATEGY:
#	1. Create a delegation root with zoned_uid set
#	2. Create a child dataset
#	3. Set properties on the child, then inherit them from user namespace
#	4. Verify properties were inherited correctly
#	5. Test inheriting both native and user properties
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Authorized user namespace can inherit properties on delegated datasets"
log_onexit cleanup

# Create delegation root with child
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    userprop,compression
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/child"

log_note "Created delegation root with child dataset"

# Set a native property on child that we will then inherit
log_must zfs set compression=lz4 "$TESTPOOL/$TESTFS/deleg_root/child"

typeset actual_comp
actual_comp=$(zfs get -H -o value compression "$TESTPOOL/$TESTFS/deleg_root/child")
if [[ "$actual_comp" != "lz4" ]]; then
	log_fail "Failed to set compression: expected lz4, got $actual_comp"
fi

# Set a user property on child that we will then inherit
log_must zfs set com.example:testprop=localvalue "$TESTPOOL/$TESTFS/deleg_root/child"

typeset actual_userprop
actual_userprop=$(zfs get -H -o value com.example:testprop "$TESTPOOL/$TESTFS/deleg_root/child")
if [[ "$actual_userprop" != "localvalue" ]]; then
	log_fail "Failed to set user property: expected localvalue, got $actual_userprop"
fi

# Test 1: Inherit native property from user namespace
log_note "Test 1: Inheriting native property from user namespace..."
typeset inherit_result
inherit_result=$(run_in_userns "$ZONED_TEST_UID" \
    inherit compression "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
inherit_status=$?

if [[ $inherit_status -ne 0 ]]; then
	log_note "Inherit compression output: $inherit_result"
	log_fail "Failed to inherit compression from user namespace"
fi

# Verify compression was inherited (should match parent's value)
actual_comp=$(zfs get -H -o value compression "$TESTPOOL/$TESTFS/deleg_root/child")
typeset comp_source
comp_source=$(zfs get -H -o source compression "$TESTPOOL/$TESTFS/deleg_root/child")
if [[ "$comp_source" == "local" ]]; then
	log_fail "Compression still local after inherit: $actual_comp (source=$comp_source)"
fi
log_note "Compression inherited successfully (value=$actual_comp, source=$comp_source)"

# Test 2: Inherit user property from user namespace
log_note "Test 2: Inheriting user property from user namespace..."
typeset inherit_userprop_result
inherit_userprop_result=$(run_in_userns "$ZONED_TEST_UID" \
    inherit com.example:testprop "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
inherit_userprop_status=$?

if [[ $inherit_userprop_status -ne 0 ]]; then
	log_note "Inherit user property output: $inherit_userprop_result"
	log_fail "Failed to inherit user property from user namespace"
fi

# Verify user property was removed (inherited means no local value)
actual_userprop=$(zfs get -H -o value com.example:testprop "$TESTPOOL/$TESTFS/deleg_root/child")
if [[ "$actual_userprop" == "localvalue" ]]; then
	log_fail "User property still has local value after inherit"
fi
log_note "User property inherited successfully (value=$actual_userprop)"

log_pass "Authorized user namespace can inherit properties on delegated datasets"
