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
#	Verify that invalid zoned_uid values are rejected.
#
# STRATEGY:
#	1. Try to set zoned_uid with invalid string value
#	2. Verify it fails
#	3. Try to set zoned_uid with negative value
#	4. Verify it fails
#

verify_runnable "global"

function cleanup
{
	if datasetexists "$TESTPOOL/$TESTFS/neg_test"; then
		log_must zfs destroy -rf "$TESTPOOL/$TESTFS/neg_test"
	fi
}

log_assert "Invalid zoned_uid values are rejected"
log_onexit cleanup

# Create test dataset
log_must zfs create "$TESTPOOL/$TESTFS/neg_test"

# Try invalid string value
log_mustnot zfs set zoned_uid=invalid "$TESTPOOL/$TESTFS/neg_test"
log_note "Invalid string value rejected"

# Try negative value (if shell allows it)
log_mustnot zfs set zoned_uid=-1 "$TESTPOOL/$TESTFS/neg_test"
log_note "Negative value rejected"

# Verify dataset still has default value
typeset val
val=$(get_zoned_uid "$TESTPOOL/$TESTFS/neg_test")
if [[ "$val" != "0" ]]; then
	log_fail "zoned_uid should still be 0 after failed sets, got: $val"
fi
log_note "zoned_uid unchanged after invalid set attempts"

log_pass "Invalid zoned_uid values are rejected"
