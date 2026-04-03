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
#	Verify that namespace-initiated rename+destroy properly cleans up
#	kernel-side zone tracking entries.  When a namespace user renames
#	a dataset, a tracking entry is created for the new name.  When
#	the renamed dataset is subsequently destroyed, that tracking entry
#	must be removed.  If it persists (stale), the delegation root
#	remains visible as a parent dataset even after the admin removes
#	the zoned_uid delegation — an information leak.
#
# STRATEGY:
#	1. Create delegation root with zoned_uid, grant permissions
#	2. From namespace: rename child → child2 (creates tracking entry)
#	3. From namespace: destroy child2 (should clean up tracking entry)
#	4. Admin removes zoned_uid delegation (zfs set zoned_uid=0)
#	5. Verify delegation root is NOT visible from the old namespace
#	   (if stale tracking persists, it would still be visible)
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Zone tracking cleanup after namespace rename+destroy"
log_onexit cleanup

# Setup: delegation root with child, mountpoint=none to avoid mount-lock issues
log_must zfs create -o mountpoint=none "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/child"

# Grant all needed permissions
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    "create,destroy,rename,mount"

# Step 1: From namespace, rename child → child2
# This internally calls zone_dataset_attach_uid for the new name
log_note "Test: rename child from namespace"
typeset result
result=$(run_in_userns "$ZONED_TEST_UID" \
    rename "$TESTPOOL/$TESTFS/deleg_root/child" \
    "$TESTPOOL/$TESTFS/deleg_root/child2" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "rename should succeed from namespace"
fi
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/child2"
log_note "Rename succeeded"

# Step 2: From namespace, destroy child2
# This should clean up the tracking entry created by rename
log_note "Test: destroy renamed child from namespace"
result=$(run_in_userns "$ZONED_TEST_UID" \
    destroy "$TESTPOOL/$TESTFS/deleg_root/child2" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "destroy should succeed from namespace"
fi
log_note "Destroy succeeded"

# Step 3: Admin removes the zoned_uid delegation
log_note "Test: admin removes zoned_uid delegation"
log_must zfs set zoned_uid=0 "$TESTPOOL/$TESTFS/deleg_root"

# Step 4: Verify the delegation root is NOT visible from the old namespace.
# If the tracking entry from the rename was not cleaned up (stale),
# the delegation root would still be visible as a parent of the stale
# entry, leaking its existence after delegation was revoked.
log_note "Test: verify no stale visibility after delegation removal"
result=$(run_in_userns "$ZONED_TEST_UID" \
    list "$TESTPOOL/$TESTFS/deleg_root" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "Delegation root should NOT be visible after " \
	    "zoned_uid=0 (stale tracking entry detected)"
fi
log_note "No stale visibility: delegation root correctly hidden"

log_pass "Zone tracking cleanup after namespace rename+destroy"
