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
#	Verify that snapshots can be individually destroyed from within a
#	delegated user namespace. Covers the zone_dataset_check_list()
#	visibility fix for '@' separator.
#
# STRATEGY:
#	1. Create delegation root with zoned_uid
#	2. From namespace: create child, create snapshot on child
#	3. From namespace: verify snapshot is visible via zfs list -t snapshot
#	4. From namespace: destroy snapshot individually (zfs destroy ds@snap)
#	5. Verify snapshot is gone
#	6. From namespace: create snapshot on delegation root itself
#	7. From namespace: destroy that snapshot individually
#	8. Verify snapshot is gone
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Individual snapshot destroy works from delegated user namespace"
log_onexit cleanup

# Step 1: Create delegation root
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    create,mount,snapshot,destroy

# Step 2: Create child and snapshot from namespace
typeset result
result=$(run_in_userns "$ZONED_TEST_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root/child1" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Create child output: $result"
	log_fail "Failed to create child from namespace"
fi

result=$(run_in_userns "$ZONED_TEST_UID" \
    snapshot "$TESTPOOL/$TESTFS/deleg_root/child1@snap1" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Create snapshot output: $result"
	log_fail "Failed to create snapshot from namespace"
fi

log_note "Created child1@snap1 from namespace"

# Step 3: Verify snapshot is visible from namespace
result=$(run_in_userns "$ZONED_TEST_UID" \
    list -t snapshot "$TESTPOOL/$TESTFS/deleg_root/child1@snap1" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "List snapshot output: $result"
	log_fail "Snapshot not visible from namespace"
fi
log_note "Snapshot visible from namespace"

# Step 4: Destroy snapshot individually from namespace
result=$(run_in_userns "$ZONED_TEST_UID" \
    destroy "$TESTPOOL/$TESTFS/deleg_root/child1@snap1" 2>&1)
typeset status=$?

if [[ $status -ne 0 ]]; then
	log_note "Destroy snapshot output: $result"
	log_fail "Failed to destroy individual snapshot from namespace (status=$status)"
fi

# Step 5: Verify snapshot is gone
if zfs list -t snapshot "$TESTPOOL/$TESTFS/deleg_root/child1@snap1" 2>/dev/null; then
	log_fail "Snapshot child1@snap1 should have been destroyed"
fi
log_note "child1@snap1 destroyed successfully from namespace"

# Step 6: Create snapshot on delegation root itself, then destroy it
result=$(run_in_userns "$ZONED_TEST_UID" \
    snapshot "$TESTPOOL/$TESTFS/deleg_root@rootsnap" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Create root snapshot output: $result"
	log_fail "Failed to create snapshot on delegation root"
fi

log_note "Created deleg_root@rootsnap from namespace"

# Step 7: Destroy the root snapshot individually from namespace
result=$(run_in_userns "$ZONED_TEST_UID" \
    destroy "$TESTPOOL/$TESTFS/deleg_root@rootsnap" 2>&1)
status=$?

if [[ $status -ne 0 ]]; then
	log_note "Destroy root snapshot output: $result"
	log_fail "Failed to destroy root snapshot from namespace (status=$status)"
fi

# Step 8: Verify root snapshot is gone
if zfs list -t snapshot "$TESTPOOL/$TESTFS/deleg_root@rootsnap" 2>/dev/null; then
	log_fail "Snapshot deleg_root@rootsnap should have been destroyed"
fi
log_note "deleg_root@rootsnap destroyed successfully from namespace"

log_pass "Individual snapshot destroy works from delegated user namespace"
