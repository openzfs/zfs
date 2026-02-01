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
	zfs destroy -rf $TESTPOOL/$TESTFS/deleg_root 2>/dev/null
}

log_assert "Authorized user namespace can inherit properties on delegated datasets"
log_onexit cleanup

# Create delegation root with child
log_must zfs create $TESTPOOL/$TESTFS/deleg_root
log_must set_zoned_uid $TESTPOOL/$TESTFS/deleg_root $ZONED_TEST_UID
log_must zfs create $TESTPOOL/$TESTFS/deleg_root/child

log_note "Created delegation root with child dataset"

# Set a native property on child that we will then inherit
log_must zfs set compression=lz4 $TESTPOOL/$TESTFS/deleg_root/child

typeset actual_comp=$(zfs get -H -o value compression $TESTPOOL/$TESTFS/deleg_root/child)
if [[ "$actual_comp" != "lz4" ]]; then
	log_fail "Failed to set compression: expected lz4, got $actual_comp"
fi

# Set a user property on child that we will then inherit
log_must zfs set com.example:testprop=localvalue $TESTPOOL/$TESTFS/deleg_root/child

typeset actual_userprop=$(zfs get -H -o value com.example:testprop $TESTPOOL/$TESTFS/deleg_root/child)
if [[ "$actual_userprop" != "localvalue" ]]; then
	log_fail "Failed to set user property: expected localvalue, got $actual_userprop"
fi

# Test 1: Inherit native property from user namespace
log_note "Test 1: Inheriting native property from user namespace..."
typeset inherit_result
inherit_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs inherit compression $TESTPOOL/$TESTFS/deleg_root/child 2>&1)
typeset inherit_status=$?

if [[ $inherit_status -ne 0 ]]; then
	log_note "Inherit compression output: $inherit_result"
	log_fail "Failed to inherit compression from user namespace"
fi

# Verify compression was inherited (should match parent's value)
actual_comp=$(zfs get -H -o value compression $TESTPOOL/$TESTFS/deleg_root/child)
typeset comp_source=$(zfs get -H -o source compression $TESTPOOL/$TESTFS/deleg_root/child)
if [[ "$comp_source" == "local" ]]; then
	log_fail "Compression still local after inherit: $actual_comp (source=$comp_source)"
fi
log_note "Compression inherited successfully (value=$actual_comp, source=$comp_source)"

# Test 2: Inherit user property from user namespace
log_note "Test 2: Inheriting user property from user namespace..."
typeset inherit_userprop_result
inherit_userprop_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs inherit com.example:testprop $TESTPOOL/$TESTFS/deleg_root/child 2>&1)
typeset inherit_userprop_status=$?

if [[ $inherit_userprop_status -ne 0 ]]; then
	log_note "Inherit user property output: $inherit_userprop_result"
	log_fail "Failed to inherit user property from user namespace"
fi

# Verify user property was removed (inherited means no local value)
actual_userprop=$(zfs get -H -o value com.example:testprop $TESTPOOL/$TESTFS/deleg_root/child)
if [[ "$actual_userprop" == "localvalue" ]]; then
	log_fail "User property still has local value after inherit"
fi
log_note "User property inherited successfully (value=$actual_userprop)"

log_pass "Authorized user namespace can inherit properties on delegated datasets"
