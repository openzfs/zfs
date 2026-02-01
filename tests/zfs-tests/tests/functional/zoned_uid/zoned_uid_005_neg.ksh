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
	if datasetexists $TESTPOOL/$TESTFS/neg_test; then
		log_must zfs destroy -rf $TESTPOOL/$TESTFS/neg_test
	fi
}

log_assert "Invalid zoned_uid values are rejected"
log_onexit cleanup

# Create test dataset
log_must zfs create $TESTPOOL/$TESTFS/neg_test

# Try invalid string value
log_mustnot zfs set zoned_uid=invalid $TESTPOOL/$TESTFS/neg_test
log_note "Invalid string value rejected"

# Try negative value (if shell allows it)
log_mustnot zfs set zoned_uid=-1 $TESTPOOL/$TESTFS/neg_test
log_note "Negative value rejected"

# Verify dataset still has default value
typeset val=$(get_zoned_uid $TESTPOOL/$TESTFS/neg_test)
if [[ "$val" != "0" ]]; then
	log_fail "zoned_uid should still be 0 after failed sets, got: $val"
fi
log_note "zoned_uid unchanged after invalid set attempts"

log_pass "Invalid zoned_uid values are rejected"
