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
