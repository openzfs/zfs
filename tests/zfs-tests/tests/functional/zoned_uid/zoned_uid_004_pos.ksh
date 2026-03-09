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
#	Verify that zoned_uid property is inherited by child datasets
#	and can be overridden with a different value.
#
# STRATEGY:
#	1. Create parent dataset with zoned_uid
#	2. Create child dataset
#	3. Verify child inherits parent's zoned_uid value
#	4. Override zoned_uid on child with a different value
#	5. Verify each dataset has its own value
#

verify_runnable "global"

function cleanup
{
	log_must zfs destroy -rf "$TESTPOOL/$TESTFS/parent"
}

log_assert "zoned_uid property is inherited by child datasets"
log_onexit cleanup

# Create parent dataset with zoned_uid
log_must zfs create "$TESTPOOL/$TESTFS/parent"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/parent" "$ZONED_TEST_UID"

# Create child dataset
log_must zfs create "$TESTPOOL/$TESTFS/parent/child"

# Verify child inherits parent's value
typeset child_val
child_val=$(get_zoned_uid "$TESTPOOL/$TESTFS/parent/child")
if [[ "$child_val" != "$ZONED_TEST_UID" ]]; then
	log_fail "Child zoned_uid should inherit $ZONED_TEST_UID, got: $child_val"
fi
log_note "Child dataset inherits zoned_uid=$ZONED_TEST_UID from parent"

# Verify parent still has its value
typeset parent_val
parent_val=$(get_zoned_uid "$TESTPOOL/$TESTFS/parent")
if [[ "$parent_val" != "$ZONED_TEST_UID" ]]; then
	log_fail "Parent zoned_uid should be $ZONED_TEST_UID, got: $parent_val"
fi
log_note "Parent dataset retains zoned_uid=$ZONED_TEST_UID"

# Override with different value on child
log_must set_zoned_uid "$TESTPOOL/$TESTFS/parent/child" "$ZONED_OTHER_UID"

# Verify each has independent value
parent_val=$(get_zoned_uid "$TESTPOOL/$TESTFS/parent")
child_val=$(get_zoned_uid "$TESTPOOL/$TESTFS/parent/child")

if [[ "$parent_val" != "$ZONED_TEST_UID" ]]; then
	log_fail "Parent zoned_uid changed unexpectedly to: $parent_val"
fi
if [[ "$child_val" != "$ZONED_OTHER_UID" ]]; then
	log_fail "Child zoned_uid should be $ZONED_OTHER_UID, got: $child_val"
fi
log_note "Parent and child have independent zoned_uid values after override"

log_pass "zoned_uid property is inherited by child datasets"
