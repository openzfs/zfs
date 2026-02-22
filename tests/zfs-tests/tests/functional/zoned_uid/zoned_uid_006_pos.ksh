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
#	Verify that an authorized user namespace can create child datasets
#	under a delegation root with matching zoned_uid.
#
# STRATEGY:
#	1. Create a test dataset and set zoned_uid to test UID
#	2. Enter a user namespace owned by that UID
#	3. Verify CAP_SYS_ADMIN is present in the namespace
#	4. Attempt to create a child dataset
#	5. Verify the child dataset was created successfully
#

verify_runnable "global"

function cleanup
{
	# Clean up from global zone
	zfs destroy -rf $TESTPOOL/$TESTFS/deleg_root 2>/dev/null
}

log_assert "Authorized user namespace can create child datasets"
log_onexit cleanup

# Create delegation root and set zoned_uid
log_must zfs create $TESTPOOL/$TESTFS/deleg_root
log_must set_zoned_uid $TESTPOOL/$TESTFS/deleg_root $ZONED_TEST_UID

# Verify zoned_uid is set
typeset actual_uid=$(get_zoned_uid $TESTPOOL/$TESTFS/deleg_root)
if [[ "$actual_uid" != "$ZONED_TEST_UID" ]]; then
	log_fail "zoned_uid not set correctly: expected $ZONED_TEST_UID, got $actual_uid"
fi
log_note "Delegation root created with zoned_uid=$ZONED_TEST_UID"

#
# Enter user namespace and attempt to create child dataset.
# unshare --user creates a new user namespace where the caller
# has CAP_SYS_ADMIN (and all other capabilities) within that namespace.
#
# The --map-user option maps the current user to root inside the namespace,
# which is the standard rootless container setup.
#
log_note "Attempting to create child dataset from user namespace..."

# Use sudo -u to run as the zoned_uid owner, then unshare into user namespace
# The user namespace owner will be ZONED_TEST_UID
typeset create_result
create_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs create $TESTPOOL/$TESTFS/deleg_root/child 2>&1)
typeset create_status=$?

if [[ $create_status -ne 0 ]]; then
	log_note "Create output: $create_result"
	log_fail "Failed to create child dataset from user namespace (status=$create_status)"
fi

log_note "Child dataset created successfully from user namespace"

# Verify the child exists (from global zone)
log_must zfs list $TESTPOOL/$TESTFS/deleg_root/child
log_note "Child dataset verified from global zone"

# Verify the child is visible from the user namespace
typeset list_result
list_result=$(sudo -u \#${ZONED_TEST_UID} unshare --user --mount --map-root-user \
    zfs list $TESTPOOL/$TESTFS/deleg_root/child 2>&1)
typeset list_status=$?

if [[ $list_status -ne 0 ]]; then
	log_note "List output: $list_result"
	log_fail "Child dataset not visible from user namespace"
fi

log_note "Child dataset visible from user namespace"

log_pass "Authorized user namespace can create child datasets"
