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
#	Verify that the 'zoned' property cannot be modified from within
#	a delegated namespace. The ZFS_PROP_ZONED case blocks this via
#	!INGLOBALZONE(curproc), but this is never tested in the zoned_uid
#	delegation context.
#
# STRATEGY:
#	1. Create delegation root with zoned_uid
#	2. Create child dataset
#	3. From namespace: attempt zfs set zoned=on on child (should FAIL)
#	4. From namespace: attempt zfs set zoned=off on child (should FAIL)
#	5. From namespace: attempt zfs set zoned=on on delegation root (FAIL)
#	6. Verify zoned property unchanged on all datasets
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Cannot set 'zoned' property from delegated namespace"
log_onexit cleanup

# Step 1-2: Create delegation root and child
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/child"

log_note "Created delegation root and child"

# Record original zoned values
typeset orig_root_zoned orig_child_zoned
orig_root_zoned=$(zfs get -H -o value zoned "$TESTPOOL/$TESTFS/deleg_root")
orig_child_zoned=$(zfs get -H -o value zoned "$TESTPOOL/$TESTFS/deleg_root/child")

# Step 3: Attempt zfs set zoned=on on child (should FAIL)
log_note "Test 1: zfs set zoned=on on child from namespace..."
typeset result
result=$(run_in_userns "$ZONED_TEST_UID" \
    set zoned=on "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "Setting zoned=on on child should have been denied"
fi
log_note "Correctly denied: $result"

# Step 4: Attempt zfs set zoned=off on child (should FAIL)
log_note "Test 2: zfs set zoned=off on child from namespace..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    set zoned=off "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "Setting zoned=off on child should have been denied"
fi
log_note "Correctly denied: $result"

# Step 5: Attempt zfs set zoned=on on delegation root (should FAIL)
log_note "Test 3: zfs set zoned=on on delegation root from namespace..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    set zoned=on "$TESTPOOL/$TESTFS/deleg_root" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "Setting zoned=on on delegation root should have been denied"
fi
log_note "Correctly denied: $result"

# Step 6: Verify zoned property unchanged on all datasets
typeset cur_root_zoned cur_child_zoned
cur_root_zoned=$(zfs get -H -o value zoned "$TESTPOOL/$TESTFS/deleg_root")
cur_child_zoned=$(zfs get -H -o value zoned "$TESTPOOL/$TESTFS/deleg_root/child")

if [[ "$cur_root_zoned" != "$orig_root_zoned" ]]; then
	log_fail "Root zoned changed from '$orig_root_zoned' to '$cur_root_zoned'"
fi
if [[ "$cur_child_zoned" != "$orig_child_zoned" ]]; then
	log_fail "Child zoned changed from '$orig_child_zoned' to '$cur_child_zoned'"
fi
log_note "zoned property unchanged on all datasets"

log_pass "Cannot set 'zoned' property from delegated namespace"
