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
#	Verify that destroying and recreating a pool with zoned_uid works
#	without stale kernel state. Exercises the spa_export_os() cleanup
#	path that must detach zone_uid_datasets entries on pool destroy.
#
# STRATEGY:
#	1. Create a delegation root with zoned_uid set
#	2. Create child datasets with inherited zoned_uid
#	3. Verify delegation works (create from namespace)
#	4. Destroy the pool
#	5. Recreate the pool with same zoned_uid
#	6. Verify delegation works again on the new pool
#

verify_runnable "global"

function cleanup
{
	if poolexists "$TESTPOOL"; then
		# Ensure pool is in a clean state
		zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
	else
		# Pool was destroyed by test; recreate it for the framework
		DISK=${DISKS%% *}
		default_setup_noexit "$DISK"
	fi
}

log_assert "Pool destroy/recreate with zoned_uid works without stale state"
log_onexit cleanup

# Step 1-2: Create delegation root with children
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    create,mount
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/child1"

log_note "Created delegation root with child, zoned_uid=$ZONED_TEST_UID"

# Step 3: Verify delegation works
typeset result
result=$(run_in_userns "$ZONED_TEST_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root/ns_child" 2>&1)
typeset status=$?

if [[ $status -ne 0 ]]; then
	log_note "Create output: $result"
	log_fail "Initial delegation failed (status=$status)"
fi
log_note "Initial delegation works: created ns_child from namespace"

# Step 4: Destroy the pool
log_must zpool destroy "$TESTPOOL"

log_note "Pool destroyed"

# Step 5: Recreate the pool with same zoned_uid
DISK=${DISKS%% *}
log_must zpool create -f "$TESTPOOL" "$DISK"
log_must zfs create "$TESTPOOL/$TESTFS"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    create,mount

log_note "Pool recreated with zoned_uid=$ZONED_TEST_UID"

# Step 6: Verify delegation works again on the new pool
typeset result2
result2=$(run_in_userns "$ZONED_TEST_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root/ns_child2" 2>&1)
typeset status2=$?

if [[ $status2 -ne 0 ]]; then
	log_note "Create output after recreate: $result2"
	log_fail "Delegation failed after pool destroy/recreate (status=$status2)"
fi

# Verify the dataset exists
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/ns_child2"
log_note "Delegation works after pool destroy/recreate"

log_pass "Pool destroy/recreate with zoned_uid works without stale state"
