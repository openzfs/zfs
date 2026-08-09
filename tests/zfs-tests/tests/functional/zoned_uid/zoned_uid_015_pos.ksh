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
