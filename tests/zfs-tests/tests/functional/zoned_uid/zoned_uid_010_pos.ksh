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
#	Verify that an authorized user namespace can set properties on
#	datasets within the delegation subtree.
#
# STRATEGY:
#	1. Create a delegation root with zoned_uid set
#	2. Create a child dataset
#	3. Enter user namespace and set various properties
#	4. Verify properties were set correctly
#	5. Test setting properties on delegation root itself
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Authorized user namespace can set properties on delegated datasets"
log_onexit cleanup

# Create delegation root with child
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    quota,compression,atime,userprop
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/child"

log_note "Created delegation root with child dataset"

# Test 1: Set quota on child dataset
log_note "Test 1: Setting quota from user namespace..."
typeset quota_result
quota_result=$(run_in_userns "$ZONED_TEST_UID" \
    set quota=100M "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
quota_status=$?

if [[ $quota_status -ne 0 ]]; then
	log_note "Set quota output: $quota_result"
	log_fail "Failed to set quota from user namespace"
fi

# Verify quota was set
typeset actual_quota
actual_quota=$(zfs get -H -o value quota "$TESTPOOL/$TESTFS/deleg_root/child")
if [[ "$actual_quota" != "100M" ]]; then
	log_fail "Quota not set correctly: expected 100M, got $actual_quota"
fi
log_note "Quota set successfully to 100M"

# Test 2: Set compression on child dataset
log_note "Test 2: Setting compression from user namespace..."
typeset comp_result
comp_result=$(run_in_userns "$ZONED_TEST_UID" \
    set compression=lz4 "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
comp_status=$?

if [[ $comp_status -ne 0 ]]; then
	log_note "Set compression output: $comp_result"
	log_fail "Failed to set compression from user namespace"
fi

typeset actual_comp
actual_comp=$(zfs get -H -o value compression "$TESTPOOL/$TESTFS/deleg_root/child")
if [[ "$actual_comp" != "lz4" ]]; then
	log_fail "Compression not set correctly: expected lz4, got $actual_comp"
fi
log_note "Compression set successfully to lz4"

# Test 3: Set atime on delegation root
# Unmount delegation root first — setting atime triggers a remount, and
# inherited mounts are MNT_LOCKED (cannot be remounted from a child mount
# namespace).
log_must zfs unmount "$TESTPOOL/$TESTFS/deleg_root"
log_note "Test 3: Setting atime on delegation root..."
typeset atime_result
atime_result=$(run_in_userns "$ZONED_TEST_UID" \
    set atime=off "$TESTPOOL/$TESTFS/deleg_root" 2>&1)
atime_status=$?

if [[ $atime_status -ne 0 ]]; then
	log_note "Set atime output: $atime_result"
	log_fail "Failed to set atime on delegation root"
fi

typeset actual_atime
actual_atime=$(zfs get -H -o value atime "$TESTPOOL/$TESTFS/deleg_root")
if [[ "$actual_atime" != "off" ]]; then
	log_fail "Atime not set correctly: expected off, got $actual_atime"
fi
log_note "Atime set successfully on delegation root"

# Test 4: Set a user property
log_note "Test 4: Setting user property from user namespace..."
typeset userprop_result
userprop_result=$(run_in_userns "$ZONED_TEST_UID" \
    set com.example:testprop=testvalue "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
userprop_status=$?

if [[ $userprop_status -ne 0 ]]; then
	log_note "Set user property output: $userprop_result"
	log_fail "Failed to set user property from user namespace"
fi

typeset actual_userprop
actual_userprop=$(zfs get -H -o value com.example:testprop "$TESTPOOL/$TESTFS/deleg_root/child")
if [[ "$actual_userprop" != "testvalue" ]]; then
	log_fail "User property not set correctly: expected testvalue, got $actual_userprop"
fi
log_note "User property set successfully"

# Test 5: Verify properties are visible from user namespace
log_note "Test 5: Verifying properties visible from user namespace..."
typeset get_result
get_result=$(run_in_userns "$ZONED_TEST_UID" \
    get quota,compression "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
get_status=$?

if [[ $get_status -ne 0 ]]; then
	log_note "Get properties output: $get_result"
	log_fail "Failed to get properties from user namespace"
fi

log_note "Properties visible from user namespace"

log_pass "Authorized user namespace can set properties on delegated datasets"
