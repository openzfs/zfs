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
#	Verify that an authorized user namespace can create sub-datasets
#	(grandchildren) under a delegation root. The zoned_uid property
#	must inherit so that children of children are also authorized.
#
# STRATEGY:
#	1. Create a delegation root and set zoned_uid
#	2. From user namespace, create a child dataset
#	3. From user namespace, create a grandchild under the child
#	4. Verify the grandchild exists and is visible from the namespace
#	5. Verify zoned_uid inherited to both child and grandchild
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Authorized user namespace can create sub-datasets (grandchildren)"
log_onexit cleanup

# Create delegation root and set zoned_uid
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    create,mount
log_note "Delegation root created with zoned_uid=$ZONED_TEST_UID"

# Step 1: Create child from user namespace
typeset create_result
create_result=$(run_in_userns "$ZONED_TEST_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
create_status=$?

if [[ $create_status -ne 0 ]]; then
	log_note "Create child output: $create_result"
	log_fail "Failed to create child dataset (status=$create_status)"
fi
log_note "Child dataset created successfully"

# Step 2: Create grandchild from user namespace
typeset grandchild_result
grandchild_result=$(run_in_userns "$ZONED_TEST_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root/child/grandchild" 2>&1)
grandchild_status=$?

if [[ $grandchild_status -ne 0 ]]; then
	log_note "Create grandchild output: $grandchild_result"
	log_fail "Failed to create grandchild dataset (status=$grandchild_status)"
fi
log_note "Grandchild dataset created successfully"

# Step 3: Verify both exist from global zone
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/child"
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/child/grandchild"
log_note "Both datasets verified from global zone"

# Step 4: Verify grandchild is visible from user namespace
typeset list_result
list_result=$(run_in_userns "$ZONED_TEST_UID" \
    list "$TESTPOOL/$TESTFS/deleg_root/child/grandchild" 2>&1)
list_status=$?

if [[ $list_status -ne 0 ]]; then
	log_note "List output: $list_result"
	log_fail "Grandchild not visible from user namespace"
fi
log_note "Grandchild visible from user namespace"

# Step 5: Verify zoned_uid inherited to child and grandchild
typeset child_uid
child_uid=$(get_zoned_uid "$TESTPOOL/$TESTFS/deleg_root/child")
typeset grandchild_uid
grandchild_uid=$(get_zoned_uid "$TESTPOOL/$TESTFS/deleg_root/child/grandchild")

if [[ "$child_uid" != "$ZONED_TEST_UID" ]]; then
	log_fail "zoned_uid not inherited to child: expected $ZONED_TEST_UID, got $child_uid"
fi
if [[ "$grandchild_uid" != "$ZONED_TEST_UID" ]]; then
	log_fail "zoned_uid not inherited to grandchild: expected $ZONED_TEST_UID, got $grandchild_uid"
fi
log_note "zoned_uid correctly inherited to child ($child_uid) and grandchild ($grandchild_uid)"

log_pass "Authorized user namespace can create sub-datasets (grandchildren)"
