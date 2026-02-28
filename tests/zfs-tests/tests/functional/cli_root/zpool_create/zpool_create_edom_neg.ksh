#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2026, Christos Longros. All rights reserved.
#

#
# DESCRIPTION:
#	Verify that zpool create with an out-of-range block size
#	reports a clear error about block size rather than a
#	misleading "invalid property value" message.
#
# STRATEGY:
#	1. Set vdev_file_logical_ashift above ASHIFT_MAX to force EDOM.
#	2. Attempt to create a pool.
#	3. Verify the error mentions "block size".
#	4. Verify it does not say "property".
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must set_tunable64 VDEV_FILE_LOGICAL_ASHIFT 9
	rm -f $TEST_BASE_DIR/vdev_edom
}

log_assert "zpool create with bad ashift reports block size error"
log_onexit cleanup

truncate -s $MINVDEVSIZE $TEST_BASE_DIR/vdev_edom

# Force ashift above ASHIFT_MAX (16) to trigger EDOM
log_must set_tunable64 VDEV_FILE_LOGICAL_ASHIFT 17

errmsg=$(zpool create $TESTPOOL $TEST_BASE_DIR/vdev_edom 2>&1)
typeset -i ret=$?

log_note "Return code: $ret"
log_note "Error message: $errmsg"

(( ret != 0 )) || log_fail "zpool create should have failed"

echo "$errmsg" | grep -qi "block size" || \
    log_fail "Error should mention block size: $errmsg"

echo "$errmsg" | grep -qi "property" && \
    log_fail "Error should not mention property: $errmsg"

log_pass "zpool create with bad ashift reports block size error"
