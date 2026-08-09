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
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Authorized user namespace can create child datasets"
log_onexit cleanup

# Create delegation root and set zoned_uid
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    create,mount

# Verify zoned_uid is set
typeset actual_uid
actual_uid=$(get_zoned_uid "$TESTPOOL/$TESTFS/deleg_root")
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
create_result=$(run_in_userns "$ZONED_TEST_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
create_status=$?

if [[ $create_status -ne 0 ]]; then
	log_note "Create output: $create_result"
	log_fail "Failed to create child dataset from user namespace (status=$create_status)"
fi

log_note "Child dataset created successfully from user namespace"

# Verify the child exists (from global zone)
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/child"
log_note "Child dataset verified from global zone"

# Verify the child is visible from the user namespace
typeset list_result
list_result=$(run_in_userns "$ZONED_TEST_UID" \
    list "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
list_status=$?

if [[ $list_status -ne 0 ]]; then
	log_note "List output: $list_result"
	log_fail "Child dataset not visible from user namespace"
fi

log_note "Child dataset visible from user namespace"

log_pass "Authorized user namespace can create child datasets"
