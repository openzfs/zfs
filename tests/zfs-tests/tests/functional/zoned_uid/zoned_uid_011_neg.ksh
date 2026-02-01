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
#	Verify that a user namespace with a non-matching UID cannot perform
#	write operations on datasets delegated to a different UID.
#
# STRATEGY:
#	1. Create a delegation root with zoned_uid set to ZONED_TEST_UID
#	2. Enter a user namespace owned by ZONED_OTHER_UID (different)
#	3. Verify dataset is visible (read-only path visibility)
#	4. Attempt to create child dataset (should fail)
#	5. Attempt to create snapshot (should fail)
#	6. Attempt to set property (should fail)
#	7. Attempt to destroy (should fail)
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf $TESTPOOL/$TESTFS/deleg_root 2>/dev/null
}

log_assert "Unauthorized user namespace cannot perform write operations"
log_onexit cleanup

# Create delegation root owned by ZONED_TEST_UID
log_must zfs create $TESTPOOL/$TESTFS/deleg_root
log_must set_zoned_uid $TESTPOOL/$TESTFS/deleg_root $ZONED_TEST_UID
log_must zfs create $TESTPOOL/$TESTFS/deleg_root/child

log_note "Created delegation root with zoned_uid=$ZONED_TEST_UID"
log_note "Will test access from user namespace owned by $ZONED_OTHER_UID"

# Test 1: Verify dataset visibility (should be visible via parent path)
# Note: The dataset may or may not be visible depending on implementation
# The key test is that write operations fail
log_note "Test 1: Checking visibility from wrong user namespace..."
typeset list_result
list_result=$(sudo -u \#${ZONED_OTHER_UID} unshare --user --mount --map-root-user \
    zfs list $TESTPOOL/$TESTFS/deleg_root 2>&1)
typeset list_status=$?
log_note "List result (status=$list_status): $list_result"

# Test 2: Attempt to create child dataset (should FAIL)
log_note "Test 2: Attempting to create child from wrong namespace (should fail)..."
typeset create_result
create_result=$(sudo -u \#${ZONED_OTHER_UID} unshare --user --mount --map-root-user \
    zfs create $TESTPOOL/$TESTFS/deleg_root/unauthorized_child 2>&1)
typeset create_status=$?

if [[ $create_status -eq 0 ]]; then
	log_fail "Creating child from unauthorized namespace should have been denied"
fi
log_note "Create correctly denied: $create_result"

# Verify the unauthorized child was not created
if zfs list $TESTPOOL/$TESTFS/deleg_root/unauthorized_child 2>/dev/null; then
	log_fail "Unauthorized child dataset should not exist"
fi

# Test 3: Attempt to create snapshot (should FAIL)
log_note "Test 3: Attempting to create snapshot from wrong namespace (should fail)..."
typeset snap_result
snap_result=$(sudo -u \#${ZONED_OTHER_UID} unshare --user --mount --map-root-user \
    zfs snapshot $TESTPOOL/$TESTFS/deleg_root/child@unauthorized 2>&1)
typeset snap_status=$?

if [[ $snap_status -eq 0 ]]; then
	log_fail "Creating snapshot from unauthorized namespace should have been denied"
fi
log_note "Snapshot correctly denied: $snap_result"

# Test 4: Attempt to set property (should FAIL)
log_note "Test 4: Attempting to set property from wrong namespace (should fail)..."
typeset prop_result
prop_result=$(sudo -u \#${ZONED_OTHER_UID} unshare --user --mount --map-root-user \
    zfs set quota=1G $TESTPOOL/$TESTFS/deleg_root/child 2>&1)
typeset prop_status=$?

if [[ $prop_status -eq 0 ]]; then
	log_fail "Setting property from unauthorized namespace should have been denied"
fi
log_note "Set property correctly denied: $prop_result"

# Verify quota was not changed
typeset actual_quota=$(zfs get -H -o value quota $TESTPOOL/$TESTFS/deleg_root/child)
if [[ "$actual_quota" == "1G" ]]; then
	log_fail "Quota should not have been changed by unauthorized namespace"
fi

# Test 5: Attempt to destroy (should FAIL)
log_note "Test 5: Attempting to destroy from wrong namespace (should fail)..."
typeset destroy_result
destroy_result=$(sudo -u \#${ZONED_OTHER_UID} unshare --user --mount --map-root-user \
    zfs destroy $TESTPOOL/$TESTFS/deleg_root/child 2>&1)
typeset destroy_status=$?

if [[ $destroy_status -eq 0 ]]; then
	log_fail "Destroying from unauthorized namespace should have been denied"
fi
log_note "Destroy correctly denied: $destroy_result"

# Verify child still exists
log_must zfs list $TESTPOOL/$TESTFS/deleg_root/child
log_note "Child dataset still exists (protected from unauthorized access)"

# Test 6: Attempt to rename (should FAIL)
log_note "Test 6: Attempting to rename from wrong namespace (should fail)..."
typeset rename_result
rename_result=$(sudo -u \#${ZONED_OTHER_UID} unshare --user --mount --map-root-user \
    zfs rename $TESTPOOL/$TESTFS/deleg_root/child \
    $TESTPOOL/$TESTFS/deleg_root/child_renamed 2>&1)
typeset rename_status=$?

if [[ $rename_status -eq 0 ]]; then
	log_fail "Renaming from unauthorized namespace should have been denied"
fi
log_note "Rename correctly denied: $rename_result"

# Verify child still has original name
log_must zfs list $TESTPOOL/$TESTFS/deleg_root/child

log_pass "Unauthorized user namespace cannot perform write operations"
