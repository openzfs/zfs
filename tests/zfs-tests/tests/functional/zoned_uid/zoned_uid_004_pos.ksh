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
#	Verify that zoned_uid property is independent per dataset
#	(not inherited like zoned property).
#
# STRATEGY:
#	1. Create parent dataset with zoned_uid
#	2. Create child dataset
#	3. Verify child has default zoned_uid (0), not parent's value
#	4. Set different zoned_uid on child
#	5. Verify each dataset has its own value
#

verify_runnable "global"

function cleanup
{
	log_must zfs destroy -rf $TESTPOOL/$TESTFS/parent
}

log_assert "zoned_uid property is per-dataset, not inherited"
log_onexit cleanup

# Create parent dataset with zoned_uid
log_must zfs create $TESTPOOL/$TESTFS/parent
log_must set_zoned_uid $TESTPOOL/$TESTFS/parent $ZONED_TEST_UID

# Create child dataset
log_must zfs create $TESTPOOL/$TESTFS/parent/child

# Verify child has default value (0), not inherited
typeset child_val=$(get_zoned_uid $TESTPOOL/$TESTFS/parent/child)
if [[ "$child_val" != "0" ]]; then
	log_fail "Child zoned_uid should be 0 (default), not inherited. Got: $child_val"
fi
log_note "Child dataset has default zoned_uid=0 (not inherited)"

# Verify parent still has its value
typeset parent_val=$(get_zoned_uid $TESTPOOL/$TESTFS/parent)
if [[ "$parent_val" != "$ZONED_TEST_UID" ]]; then
	log_fail "Parent zoned_uid should be $ZONED_TEST_UID, got: $parent_val"
fi
log_note "Parent dataset retains zoned_uid=$ZONED_TEST_UID"

# Set different value on child
log_must set_zoned_uid $TESTPOOL/$TESTFS/parent/child $ZONED_OTHER_UID

# Verify each has independent value
parent_val=$(get_zoned_uid $TESTPOOL/$TESTFS/parent)
child_val=$(get_zoned_uid $TESTPOOL/$TESTFS/parent/child)

if [[ "$parent_val" != "$ZONED_TEST_UID" ]]; then
	log_fail "Parent zoned_uid changed unexpectedly to: $parent_val"
fi
if [[ "$child_val" != "$ZONED_OTHER_UID" ]]; then
	log_fail "Child zoned_uid should be $ZONED_OTHER_UID, got: $child_val"
fi
log_note "Parent and child have independent zoned_uid values"

log_pass "zoned_uid property is per-dataset, not inherited"
