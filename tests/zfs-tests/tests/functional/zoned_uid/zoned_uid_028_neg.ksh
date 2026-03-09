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
#	Verify that the additive model does not affect non-zoned datasets.
#	Standard ZFS permission checks (secpolicy_zfs → dsl_deleg) continue
#	to work unchanged when zone_dataset_admin_check returns NOT_APPLICABLE.
#
# STRATEGY:
#	1. Create a dataset WITHOUT zoned_uid
#	2. From global zone as root: all operations succeed (existing behavior)
#	3. Grant permissions to a non-root user via zfs allow
#	4. As non-root user (not in namespace): operations succeed via dsl_deleg
#	5. Without zfs allow grant: operations fail
#	6. Verify zoned_uid model doesn't interfere
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/normal_ds" 2>/dev/null
}

log_assert "Non-zoned datasets use standard permission model unchanged"
log_onexit cleanup

# Create a normal dataset (no zoned_uid)
log_must zfs create "$TESTPOOL/$TESTFS/normal_ds"

# Verify zoned_uid is 0 (unset)
typeset val
val=$(get_zoned_uid "$TESTPOOL/$TESTFS/normal_ds")
if [[ "$val" != "0" ]]; then
	log_fail "Default zoned_uid should be 0, got: $val"
fi

# EXIST-1: Root in global zone can do everything (existing behavior)
log_note "Test EXIST-1: root in global zone"
log_must zfs create "$TESTPOOL/$TESTFS/normal_ds/child"
log_must zfs snapshot "$TESTPOOL/$TESTFS/normal_ds/child@snap"
log_must zfs destroy "$TESTPOOL/$TESTFS/normal_ds/child@snap"
log_must zfs destroy "$TESTPOOL/$TESTFS/normal_ds/child"
log_note "EXIST-1 passed: root can do everything"

# EXIST-2: Non-root with zfs allow can perform delegated operations
log_note "Test EXIST-2: non-root with zfs allow"
log_must grant_deleg "$TESTPOOL/$TESTFS/normal_ds" "$ZONED_TEST_UID" \
    "create,snapshot,mount,destroy"

# Run as the test user (NOT in a namespace, just sudo -u)
typeset zfs_cmd result
zfs_cmd="$(which zfs)"
result=$(sudo -u \#"$ZONED_TEST_UID" "$zfs_cmd" \
    create "$TESTPOOL/$TESTFS/normal_ds/deleg_child" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "EXIST-2: non-root with zfs allow should be able to create"
fi
log_note "EXIST-2 passed: dsl_deleg works for non-root"

# EXIST-3: Non-root WITHOUT zfs allow is denied
log_note "Test EXIST-3: non-root without zfs allow"
log_must revoke_deleg "$TESTPOOL/$TESTFS/normal_ds" "$ZONED_TEST_UID"

result=$(sudo -u \#"$ZONED_TEST_UID" "$zfs_cmd" \
    create "$TESTPOOL/$TESTFS/normal_ds/denied_child" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "EXIST-3: non-root without zfs allow should be denied"
fi
log_note "EXIST-3 passed: denied without dsl_deleg"

# Cleanup the child we created
log_must zfs destroy "$TESTPOOL/$TESTFS/normal_ds/deleg_child"

log_pass "Non-zoned datasets use standard permission model unchanged"
