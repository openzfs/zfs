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
#	Verify that the zoned_uid property can be set and retrieved.
#
# STRATEGY:
#	1. Verify default zoned_uid is 0 (none)
#	2. Set zoned_uid to a test UID
#	3. Verify the property value is correct
#	4. Clear zoned_uid (set to 0)
#	5. Verify it returns to 0
#

verify_runnable "global"

function cleanup
{
	log_must zfs destroy -rf "$TESTPOOL/$TESTFS/zoned_test"
}

log_assert "zoned_uid property can be set and retrieved"
log_onexit cleanup

# Create test dataset
log_must zfs create "$TESTPOOL/$TESTFS/zoned_test"

# Verify default is 0
typeset default_val
default_val=$(get_zoned_uid "$TESTPOOL/$TESTFS/zoned_test")
if [[ "$default_val" != "0" ]]; then
	log_fail "Default zoned_uid should be 0, got: $default_val"
fi
log_note "Default zoned_uid is 0 as expected"

# Set zoned_uid
log_must set_zoned_uid "$TESTPOOL/$TESTFS/zoned_test" "$ZONED_TEST_UID"

# Verify the value
typeset set_val
set_val=$(get_zoned_uid "$TESTPOOL/$TESTFS/zoned_test")
if [[ "$set_val" != "$ZONED_TEST_UID" ]]; then
	log_fail "zoned_uid should be $ZONED_TEST_UID, got: $set_val"
fi
log_note "zoned_uid set to $ZONED_TEST_UID successfully"

# Clear zoned_uid
log_must clear_zoned_uid "$TESTPOOL/$TESTFS/zoned_test"

# Verify it's back to 0
typeset cleared_val
cleared_val=$(get_zoned_uid "$TESTPOOL/$TESTFS/zoned_test")
if [[ "$cleared_val" != "0" ]]; then
	log_fail "Cleared zoned_uid should be 0, got: $cleared_val"
fi
log_note "zoned_uid cleared to 0 successfully"

log_pass "zoned_uid property can be set and retrieved"
