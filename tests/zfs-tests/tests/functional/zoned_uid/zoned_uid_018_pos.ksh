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
#	Verify that clone operations work from within a delegated user
#	namespace, and that cloning outside the subtree is denied.
#
# STRATEGY:
#	1. Create delegation root with zoned_uid
#	2. From namespace: create child dataset
#	3. From namespace: create snapshot on child
#	4. From namespace: clone the snapshot to a new dataset within subtree
#	5. Verify clone exists and is writable
#	6. From namespace: attempt to clone outside the subtree (should FAIL)
#	7. Verify the failed clone doesn't exist
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
	zfs destroy -rf "$TESTPOOL/$TESTFS/outside_clone" 2>/dev/null
}

log_assert "Clone operations work from delegated user namespace"
log_onexit cleanup

# Step 1: Create delegation root
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    create,mount,snapshot,clone

# Step 2: Create child from namespace
typeset result
result=$(run_in_userns "$ZONED_TEST_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root/child" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Create child output: $result"
	log_fail "Failed to create child from namespace"
fi

# Step 3: Create snapshot from namespace
result=$(run_in_userns "$ZONED_TEST_UID" \
    snapshot "$TESTPOOL/$TESTFS/deleg_root/child@snap1" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Create snapshot output: $result"
	log_fail "Failed to create snapshot from namespace"
fi

log_note "Created child@snap1 from namespace"

# Step 4: Clone snapshot to new dataset within subtree
result=$(run_in_userns "$ZONED_TEST_UID" \
    clone "$TESTPOOL/$TESTFS/deleg_root/child@snap1" \
    "$TESTPOOL/$TESTFS/deleg_root/myclone" 2>&1)
typeset status=$?

if [[ $status -ne 0 ]]; then
	log_note "Clone output: $result"
	log_fail "Failed to clone within subtree from namespace (status=$status)"
fi

# Step 5: Verify clone exists and is writable
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/myclone"

typeset origin
origin=$(zfs get -H -o value origin "$TESTPOOL/$TESTFS/deleg_root/myclone")
if [[ "$origin" != "$TESTPOOL/$TESTFS/deleg_root/child@snap1" ]]; then
	log_fail "Clone origin should be child@snap1, got: $origin"
fi
log_note "Clone exists with correct origin"

# Verify writable: create a child under the clone from namespace
result=$(run_in_userns "$ZONED_TEST_UID" \
    create "$TESTPOOL/$TESTFS/deleg_root/myclone/subchild" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Create under clone output: $result"
	log_fail "Clone is not writable from namespace"
fi
log_note "Clone is writable from namespace"

# Step 6: Attempt to clone outside the subtree (should FAIL)
log_note "Attempting clone to outside subtree..."
result=$(run_in_userns "$ZONED_TEST_UID" \
    clone "$TESTPOOL/$TESTFS/deleg_root/child@snap1" \
    "$TESTPOOL/$TESTFS/outside_clone" 2>&1)
status=$?

if [[ $status -eq 0 ]]; then
	log_fail "Clone to outside subtree should have been denied"
fi
log_note "Correctly denied clone to outside subtree: $result"

# Step 7: Verify the failed clone doesn't exist
if datasetexists "$TESTPOOL/$TESTFS/outside_clone"; then
	log_fail "Outside clone should not exist"
fi
log_note "Outside clone correctly does not exist"

log_pass "Clone operations work from delegated user namespace"
