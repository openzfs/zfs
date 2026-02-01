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
#	Verify that zoned_uid property persists through pool export/import.
#
# STRATEGY:
#	1. Create a test dataset
#	2. Set zoned_uid property
#	3. Export the pool
#	4. Import the pool
#	5. Verify zoned_uid property is preserved
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	DISK=${DISKS%% *}
	default_setup $DISK
}

log_assert "zoned_uid property persists through pool export/import"
log_onexit cleanup

# Create test dataset
log_must zfs create $TESTPOOL/$TESTFS/persist_test

# Set zoned_uid
log_must set_zoned_uid $TESTPOOL/$TESTFS/persist_test $ZONED_TEST_UID

# Verify before export
typeset before_val=$(get_zoned_uid $TESTPOOL/$TESTFS/persist_test)
if [[ "$before_val" != "$ZONED_TEST_UID" ]]; then
	log_fail "Before export: zoned_uid should be $ZONED_TEST_UID, got: $before_val"
fi
log_note "zoned_uid is $ZONED_TEST_UID before export"

# Export the pool
log_must zpool export $TESTPOOL

# Import the pool
log_must zpool import $TESTPOOL

# Verify after import
typeset after_val=$(get_zoned_uid $TESTPOOL/$TESTFS/persist_test)
if [[ "$after_val" != "$ZONED_TEST_UID" ]]; then
	log_fail "After import: zoned_uid should be $ZONED_TEST_UID, got: $after_val"
fi
log_note "zoned_uid is $ZONED_TEST_UID after import"

# Cleanup
log_must zfs destroy $TESTPOOL/$TESTFS/persist_test

log_pass "zoned_uid property persists through pool export/import"
