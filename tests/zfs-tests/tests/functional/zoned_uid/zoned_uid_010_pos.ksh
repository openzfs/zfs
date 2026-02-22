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
	zfs destroy -rf $TESTPOOL/$TESTFS/deleg_root 2>/dev/null
}

log_assert "Authorized user namespace can set properties on delegated datasets"
log_onexit cleanup

# Create delegation root with child
log_must zfs create $TESTPOOL/$TESTFS/deleg_root
log_must set_zoned_uid $TESTPOOL/$TESTFS/deleg_root $ZONED_TEST_UID
log_must zfs create $TESTPOOL/$TESTFS/deleg_root/child

log_note "Created delegation root with child dataset"

# Test 1: Set quota on child dataset
log_note "Test 1: Setting quota from user namespace..."
typeset quota_result
quota_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs set quota=100M $TESTPOOL/$TESTFS/deleg_root/child 2>&1)
typeset quota_status=$?

if [[ $quota_status -ne 0 ]]; then
	log_note "Set quota output: $quota_result"
	log_fail "Failed to set quota from user namespace"
fi

# Verify quota was set
typeset actual_quota=$(zfs get -H -o value quota $TESTPOOL/$TESTFS/deleg_root/child)
if [[ "$actual_quota" != "100M" ]]; then
	log_fail "Quota not set correctly: expected 100M, got $actual_quota"
fi
log_note "Quota set successfully to 100M"

# Test 2: Set compression on child dataset
log_note "Test 2: Setting compression from user namespace..."
typeset comp_result
comp_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs set compression=lz4 $TESTPOOL/$TESTFS/deleg_root/child 2>&1)
typeset comp_status=$?

if [[ $comp_status -ne 0 ]]; then
	log_note "Set compression output: $comp_result"
	log_fail "Failed to set compression from user namespace"
fi

typeset actual_comp=$(zfs get -H -o value compression $TESTPOOL/$TESTFS/deleg_root/child)
if [[ "$actual_comp" != "lz4" ]]; then
	log_fail "Compression not set correctly: expected lz4, got $actual_comp"
fi
log_note "Compression set successfully to lz4"

# Test 3: Set atime on delegation root
# Unmount delegation root first — setting atime triggers a remount, and
# inherited mounts are MNT_LOCKED (cannot be remounted from a child mount
# namespace).
log_must zfs unmount $TESTPOOL/$TESTFS/deleg_root
log_note "Test 3: Setting atime on delegation root..."
typeset atime_result
atime_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs set atime=off $TESTPOOL/$TESTFS/deleg_root 2>&1)
typeset atime_status=$?

if [[ $atime_status -ne 0 ]]; then
	log_note "Set atime output: $atime_result"
	log_fail "Failed to set atime on delegation root"
fi

typeset actual_atime=$(zfs get -H -o value atime $TESTPOOL/$TESTFS/deleg_root)
if [[ "$actual_atime" != "off" ]]; then
	log_fail "Atime not set correctly: expected off, got $actual_atime"
fi
log_note "Atime set successfully on delegation root"

# Test 4: Set a user property
log_note "Test 4: Setting user property from user namespace..."
typeset userprop_result
userprop_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs set com.example:testprop=testvalue $TESTPOOL/$TESTFS/deleg_root/child 2>&1)
typeset userprop_status=$?

if [[ $userprop_status -ne 0 ]]; then
	log_note "Set user property output: $userprop_result"
	log_fail "Failed to set user property from user namespace"
fi

typeset actual_userprop=$(zfs get -H -o value com.example:testprop $TESTPOOL/$TESTFS/deleg_root/child)
if [[ "$actual_userprop" != "testvalue" ]]; then
	log_fail "User property not set correctly: expected testvalue, got $actual_userprop"
fi
log_note "User property set successfully"

# Test 5: Verify properties are visible from user namespace
log_note "Test 5: Verifying properties visible from user namespace..."
typeset get_result
get_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs get quota,compression $TESTPOOL/$TESTFS/deleg_root/child 2>&1)
typeset get_status=$?

if [[ $get_status -ne 0 ]]; then
	log_note "Get properties output: $get_result"
	log_fail "Failed to get properties from user namespace"
fi

log_note "Properties visible from user namespace"

log_pass "Authorized user namespace can set properties on delegated datasets"
